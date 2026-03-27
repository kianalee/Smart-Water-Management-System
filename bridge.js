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
    CREATE TABLE IF NOT EXISTS distance_readings (
        id INTEGER PRIMARY KEY,
        value REAL NOT NULL,
        timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
    )
    CREATE TABLE IF NOT EXISTS temperature_readings (
        id INTEGER PRIMARY KEY,
        value REAL NOT NULL,
        timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
    )
`);

console.log('SQLite database ready');

// ─── MQTT Bridge ───────────────────────────────────────────────
const mqttClient = mqtt.connect('mqtt://192.168.4.2:1883', {
    username: 'master',
    password: 'masterpass'
});

mqttClient.on('connect', () => {
    console.log('Connected to MQTT broker');
    mqttClient.subscribe('esp32/distance', { nl: true });
    mqttClient.subscribe('esp32/temp', {n1:true});
});

mqttClient.on('message', (topic, message) => {
    const value = parseFloat(message.toString());

    if (isNaN(value)) {
        console.warn(`Invalid value received on ${topic}: ${message.toString()}`);
        return;
    }

    if (topic =='esp32/distance'){
        const insert = db.prepare('INSERT INTO distance_readings (value) VALUES (?)');
        insert.run(value);
        console.log(`Saved [distance]: ${value} cm`);
    }
    else if (topic =='esp32/temp'){
        const insert = db.prepare('INSERT INTO temperature_readings (value) VALUES (?)');
        insert.run(value);
        console.log(`Saved [temperature]: ${value} °C`);
    }
});

// ─── REST API Endpoints (distance) ────────────────────────────────────────

// Get all readings
app.get('/api/distance', (req, res) => {
    const rows = db.prepare('SELECT * FROM distance_readings ORDER BY timestamp DESC').all();
    res.json(rows);
});

// Get latest reading only
app.get('/api/distance/latest', (req, res) => {
    const row = db.prepare('SELECT * FROM distance_readings ORDER BY timestamp DESC LIMIT 1').get();
    res.json(row);
});

// Get last N readings e.g. /api/distance/last/10
app.get('/api/distance/last/:n', (req, res) => {
    const n = parseInt(req.params.n);
    const rows = db.prepare('SELECT * FROM distance_readings ORDER BY timestamp DESC LIMIT ?').all(n);
    res.json(rows);
});

// Delete all readings (useful for testing)
app.delete('/api/distance', (req, res) => {
    db.prepare('DELETE FROM distance_readings').run();
    res.json({ message: 'All readings deleted' });
});
// ─── REST API Endpoints (temperature) ────────────────────────────────────────

// Get all readings
app.get('/api/temperature', (req, res) => {
    const rows = db.prepare('SELECT * FROM temperature_readings ORDER BY timestamp DESC').all();
    res.json(rows);
});

// Get latest reading only
app.get('/api/temperature/latest', (req, res) => {
    const row = db.prepare('SELECT * FROM temperature_readings ORDER BY timestamp DESC LIMIT 1').get();
    res.json(row);
});

// Get last N readings e.g. /api/distance/last/10
app.get('/api/temperature/last/:n', (req, res) => {
    const n = parseInt(req.params.n);
    const rows = db.prepare('SELECT * FROM temperature_readings ORDER BY timestamp DESC LIMIT ?').all(n);
    res.json(rows);
});

// Delete all readings (useful for testing)
app.delete('/api/temperature', (req, res) => {
    db.prepare('DELETE FROM temperature_readings').run();
    res.json({ message: 'All readings deleted' });
});

// ─── Start Server ──────────────────────────────────────────────
app.listen(3000, () => {
    console.log('Server running on http://localhost:3000');
});
