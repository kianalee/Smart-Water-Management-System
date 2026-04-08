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
        timestamp DATETIME DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now', 'localtime'))  
    );
    CREATE TABLE IF NOT EXISTS flow_readings (
        id INTEGER PRIMARY KEY,
        value REAL NOT NULL,
        timestamp DATETIME DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now', 'localtime'))  
    );
    CREATE TABLE IF NOT EXISTS system_updates (
        id INTEGER PRIMARY KEY,
        topic TEXT NOT NULL,
        value TEXT NOT NULL,
        timestamp DATETIME DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now', 'localtime'))  
    );
    CREATE TABLE IF NOT EXISTS session_starts (
        id INTEGER PRIMARY KEY,
        timestamp DATETIME DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now', 'localtime'))  
    );
    CREATE TABLE IF NOT EXISTS leak_threshold(
        id INTEGER PRIMARY KEY,
        value text NOT NULL,
        timestamp DATETIME DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now', 'localtime'))  
    );
    CREATE TABLE IF NOT EXISTS block_threshold(
        id INTEGER PRIMARY KEY,
        value text NOT NULL,
        timestamp DATETIME DEFAULT (strftime('%Y-%m-%d %H:%M:%f', 'now', 'localtime'))  
    );
    CREATE TABLE IF NOT EXISTS latency (
        id INTEGER PRIMARY KEY,
        value TEXT NOT NULL
    );
`);

console.log('SQLite database ready');

// ─── MQTT Bridge ───────────────────────────────────────────────
const mqttClient = mqtt.connect('mqtt://10.229.170.200:1883', {
    username: 'master',
    password: 'masterpass'
});

mqttClient.on('connect', () => {
    console.log('Connected to MQTT broker');

    db.prepare(`
        INSERT INTO session_starts DEFAULT VALUES
    `).run();

    mqttClient.subscribe('esp32/pressure', { nl: true });
    mqttClient.subscribe('esp32/flow', {nl:true});
    mqttClient.subscribe('esp32/pump', {nl:true});
    mqttClient.subscribe('esp32/top_valve', {nl:true});
    mqttClient.subscribe('esp32/bottom_valve', {nl:true});
    mqttClient.subscribe('system_ready', {nl:true});
    mqttClient.subscribe('system_status', {nl:true});
    mqttClient.subscribe('system_pause', {nl:true});
    mqttClient.subscribe('block_threshold', {nl:true});
    mqttClient.subscribe('leak_threshold', {nl:true});
    mqttClient.subscribe('esp32/latency_test', {nl:true});

});

mqttClient.on('message', (topic, message) => {
    if (topic =='esp32/pressure' || topic =='esp32/flow'|| topic =='block_threshold' || topic =='leak_threshold') {
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
        else if(topic =='block_threshold'){
            const insert = db.prepare('INSERT INTO block_threshold (value) VALUES (?)');
            insert.run(value);
            console.log(`Saved [block_threshold]: ${value}`);
        }
        else if(topic =='leak_threshold'){
            const insert = db.prepare('INSERT INTO leak_threshold (value) VALUES (?)');
            insert.run(value);
            console.log(`Saved [leak_threshold]: ${value}`);
        }
    }
    else if(topic == "esp32/latency_test"){
            const t0 = parseInt (message.toString());
            if (isNaN(t0)) {
                console.warn(`Invalid latency value: ${message.toString()}`);
                return;
            }   
            const t1 = Date.now(); // when bridge recieves the message
            const data = {t0: t0, t1: t1};

            const insert = db.prepare('INSERT INTO latency (value) VALUES (?)');
            insert.run(JSON.stringify(data));

            const t2 = Date.now(); 
            data.t2 = t2;


            const update = db.prepare('UPDATE latency SET value = ? WHERE id = (SELECT MAX(id) FROM latency)');
            update.run(JSON.stringify(data));
            //console.log(`Saved [latency]: ${data}`);
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

// Get latest reading for a SPECIFIC system topic (e.g., /api/system/system_status)
app.get('/api/system/latest/:topic', (req, res) => {
    const topic = req.params.topic;
    const rows = db.prepare('SELECT * FROM system_updates WHERE topic = ? ORDER BY timestamp DESC LIMIT 1').get(topic);
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

app.get('/api/water_usage/latest', (req, res) => {

    const session = db.prepare(`
        SELECT timestamp
        FROM session_starts
        ORDER BY timestamp DESC
        LIMIT 1
    `).get();

    if (!session) {
        return res.json({
            litres: 0,
            message: "no session found"
        });
    }

    const rows = db.prepare(`
        SELECT value, timestamp
        FROM flow_readings
        WHERE timestamp >= ?
        ORDER BY timestamp ASC
    `).all(session.timestamp);

    if (rows.length < 2) {
        return res.json({
            litres: 0,
            since: session.timestamp
        });
    }

    let totalLitres = 0;

    for (let i = 1; i < rows.length; i++) {

        const prev = rows[i - 1];
        const curr = rows[i];

        const t0 = new Date(prev.timestamp).getTime();
        const t1 = new Date(curr.timestamp).getTime();

        const deltaMinutes =
            (t1 - t0) / 60000;

        // trapezoidal integration
        const avgFlow =
            (prev.value + curr.value) / 2;

        totalLitres +=
            avgFlow * deltaMinutes;
    }

    res.json({
        value: totalLitres,
        timestamp: session.timestamp
    });

});

// Delete all readings (useful for testing)
app.delete('/api/system_updates', (req, res) => {
    db.prepare('DELETE FROM system_updates').run();
    res.json({ message: 'All readings deleted' });
});

//─── REST API Endpoints (threshold values) ────────────────────────────────────────
app.get('/api/thresholds/leak/latest', (req, res) => {
    const row = db.prepare('SELECT * FROM leak_threshold ORDER BY timestamp DESC LIMIT 1').get();
    res.json(row); 
});
//─── REST API Endpoints (threshold values) ────────────────────────────────────────
app.get('/api/thresholds/block/latest', (req, res) => {
    const row = db.prepare('SELECT * FROM block_threshold ORDER BY timestamp DESC LIMIT 1').get();
    res.json(row); 
});

app.get('/api/latency/latest', (req, res) => {
    const row = db.prepare(`
        SELECT * FROM latency ORDER BY id DESC LIMIT 1`).get();

    const data = JSON.parse(row.value);

    res.json(data);
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
