const express = require('express');
const mqtt = require('mqtt');
const Database = require('better-sqlite3');
const cors = require('cors');

const app = express();
app.use(express.json());
app.use(cors()); // Allows React to call the API

// ─── Database Setup ────────────────────────────────────────────
const db = new Database('sensor_data.db');

db.exec(`
    CREATE TABLE IF NOT EXISTS pressure_readings (
        id INTEGER PRIMARY KEY,
        value REAL NOT NULL,
        timestamp DATETIME DEFAULT (datetime('now', 'localtime'))
    );
    CREATE TABLE IF NOT EXISTS flow_readings (
        id INTEGER PRIMARY KEY,
        value REAL NOT NULL,
        timestamp DATETIME DEFAULT (datetime('now', 'localtime'))
    );
    CREATE TABLE IF NOT EXISTS system_updates (
        id INTEGER PRIMARY KEY,
        topic TEXT NOT NULL,
        value TEXT NOT NULL,
        timestamp DATETIME DEFAULT (datetime('now', 'localtime'))
    );
`);

console.log('SQLite database ready');

// ─── MQTT Bridge ───────────────────────────────────────────────
const mqttClient = mqtt.connect('mqtt://192.168.4.2:1883', {
    username: 'master',
    password: 'masterpass'
});

mqttClient.on('connect', () => {
    console.log('Connected to MQTT broker');
    mqttClient.subscribe('esp32/pressure', { nl: true });
    mqttClient.subscribe('esp32/flow', {nl:true});
    mqttClient.subscribe('esp32/pump', {nl:true});
    mqttClient.subscribe('esp32/top_valve', {nl:true});
    mqttClient.subscribe('esp32/bottom_valve', {nl:true});
    mqttClient.subscribe('system_ready', {nl:true});
    mqttClient.subscribe('system_status', {nl:true});
    mqttClient.subscribe('override_status', {nl:true});
});

mqttClient.on('message', (topic, message) => {
    if (topic =='esp32/pressure' || topic =='esp32/flow'){
        const value = parseFloat(message.toString());
        if (isNaN(value)) {
            console.warn(`Invalid value received on ${topic}: ${message.toString()}`);
            return;
        }
        if (topic =='esp32/pressure'){
            const insert = db.prepare('INSERT INTO pressure_readings (value) VALUES (?)');
            insert.run(value);
            console.log(`Saved [pressure]: ${value} psi`);
        }
        else if (topic =='esp32/flow'){
            const insert = db.prepare('INSERT INTO flow_readings (value) VALUES (?)');
            insert.run(value);
            console.log(`Saved [flow]: ${value} L/min`);
        }

    }
    else{
        const insert = db.prepare('INSERT INTO system_updates (topic, value) VALUES (?, ?)');
        insert.run(topic, message.toString());
        console.log(`Saved [${topic}]: ${message.toString()}`);
    }
});


// ─── REST API Endpoints (pressure) ────────────────────────────────────────

// Get all readings
app.get('/api/pressure', (req, res) => {
    const rows = db.prepare('SELECT * FROM pressure_readings ORDER BY timestamp DESC').all();
    res.json(rows);
});

// Get latest reading only
app.get('/api/pressure/latest', (req, res) => {
    const row = db.prepare('SELECT * FROM pressure_readings ORDER BY timestamp DESC LIMIT 1').get();
    res.json(row);
});

// Get last N readings e.g. /api/pressure/last/10
app.get('/api/pressure/last/:n', (req, res) => {
    const n = parseInt(req.params.n);
    const rows = db.prepare('SELECT * FROM pressure_readings ORDER BY timestamp DESC LIMIT ?').all(n);
    res.json(rows);
});

// Delete all readings (useful for testing)
app.delete('/api/pressure', (req, res) => {
    db.prepare('DELETE FROM pressure_readings').run();
    res.json({ message: 'All readings deleted' });
});
// ─── REST API Endpoints (flow) ────────────────────────────────────────

// Get all readings
app.get('/api/flow', (req, res) => {
    const rows = db.prepare('SELECT * FROM flow_readings ORDER BY timestamp DESC').all();
    res.json(rows);
});

// Get latest reading only
app.get('/api/flow/latest', (req, res) => {
    const row = db.prepare('SELECT * FROM flow_readings ORDER BY timestamp DESC LIMIT 1').get();
    res.json(row);
});

// Get last N readings e.g. /api/pressure/last/10
app.get('/api/flow/last/:n', (req, res) => {
    const n = parseInt(req.params.n);
    const rows = db.prepare('SELECT * FROM flow_readings ORDER BY timestamp DESC LIMIT ?').all(n);
    res.json(rows);
});

// Delete all readings (useful for testing)
app.delete('/api/flow', (req, res) => {
    db.prepare('DELETE FROM flow_readings').run();
    res.json({ message: 'All readings deleted' });
});

app.get('/api/system_updates', (req, res) => {
    const rows = db.prepare('SELECT * FROM system_updates ORDER BY timestamp DESC').all();
    res.json(rows);
});

// Get updates for a SPECIFIC system topic (e.g., /api/system/system_status)
app.get('/api/system/:topic', (req, res) => {
    const topic = req.params.topic;
    const rows = db.prepare('SELECT * FROM system_updates WHERE topic = ? ORDER BY timestamp DESC').all(topic);
    res.json(rows);
});

// Get the latest state of ALL system components at once (Best for React Dashboards)
app.get('/api/system/latest/all', (req, res) => {
    const query = `
        SELECT topic, value, timestamp 
        FROM system_updates 
        WHERE id IN (SELECT MAX(id) FROM system_updates GROUP BY topic)
    `;
    const rows = db.prepare(query).all();
    res.json(rows);
});

// Delete all readings (useful for testing)
app.delete('/api/system_updates', (req, res) => {
    db.prepare('DELETE FROM system_updates').run();
    res.json({ message: 'All readings deleted' });
});
// ─── REST API Endpoints (Commands) ────────────────────────────────────────

// Send a command to the ESP32 (e.g., POST { "topic": "system_ready", "message": "PAUSE" })
app.post('/api/command', (req, res) => {
    const { topic, message } = req.body;
    
    if (!topic || !message) {
        return res.status(400).json({ error: 'Both topic and message are required' });
    }

    mqttClient.publish(topic, message, (err) => {
        if (err) {
            console.error(`Failed to publish message to ${topic}:`, err);
            return res.status(500).json({ error: 'Failed to publish to MQTT' });
        }
        console.log(`Published [${topic}]: ${message}`);
        res.json({ success: true, topic, message });
    });
});

// ─── Start Server ──────────────────────────────────────────────
app.listen(3000, () => {
    console.log('Server running on http://localhost:3000');
});
