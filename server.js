const express = require('express');
const mongoose = require('mongoose');
const path = require('path');
const session = require('express-session');
const bcrypt = require('bcrypt');
const nodemailer = require('nodemailer');
const cors = require('cors');

const PORT = process.env.PORT || 3000;
let esp32Task = null; // store command for esp32
let clients = [];     // store SSE connections
const app = express();

// Middleware
app.use(express.urlencoded({ extended: true }));
app.use(express.json());
app.use(session({
    secret: 'secretsafekey123',
    resave: false,
    saveUninitialized: false
}));
app.use(cors()); // cho phép browser ở domain khác kết nối

// Connect to MongoDB
mongoose.connect('mongodb+srv://vnbtram23:g6yKiJiOXicvmGvB@cluster0.qfxfbpd.mongodb.net/SafeSense360')
    .then(() => console.log("MongoDB connected"))
    .catch(err => console.error(err));

// Models
const userSchema = new mongoose.Schema({
    email: { type: String, required: true, unique: true },
    username: { type: String, required: true },
    password: { type: String, required: true }
}, { collection: "User" });
const User = mongoose.model('User', userSchema);

const environmentSchema = new mongoose.Schema({
    time: Date,
    temperature: Number,
    humidity: Number,
    dust_density: Number,
    gas_density: Number,
    level: Number,
}, { collection: "Environment" });
const Environment = mongoose.model('Environment', environmentSchema);

// EJS view setup
app.set("views", path.join(__dirname, "views"));
app.set("view engine", "ejs");

// Static folder
app.use(express.static(path.join(__dirname, "public")));

// Pages
app.get("/login", (req, res) => res.render("pages/login"));
app.get("/register", (req, res) => res.render("pages/register"));

// Protected routes
app.get('/', requireLogin, (req, res) => res.render("pages/home"));
app.get('/chart', requireLogin, (req, res) => res.render("pages/history-report"));
app.get('/threshold', requireLogin, (req, res) => res.render("pages/threshold-setting"));
app.get('/scheduler', requireLogin, (req, res) => res.render("pages/scheduler"));

// Authentication middleware
function requireLogin(req, res, next) {
    if (!req.session.user) {
        return res.redirect('/login');
    }
    next();
}

// Register middleware
const validAccount = async (req, res, next) => {
    const { email, username, password } = req.body;
    const user = await User.findOne({ email });
    if (user) {
        return res.status(400).json({ error: "Existed email. Please try another one." });
    }
    next();
};

// Login handler
app.post('/login', async (req, res) => {
    const { username, password } = req.body;

    try {
        const user = await User.findOne({ username });
        if (user) {
            const isMatch = (password == user.password);
            console.log(`Login attempt for user: ${username},Pass: ${user.password}, Match: ${isMatch}`);
            if (isMatch) {
                req.session.user = { id: user._id, username: user.username }; // store in session
                console.log(req.session.user);
                res.redirect('/');
            } else {
                res.status(401).send(`
                    <html>
                        <body style="font-family: sans-serif; text-align: center; margin-top: 50px;">
                        <h2 style="color: red;">Invalid username or password</h2>
                        <p>Redirecting to login page in 3 seconds...</p>
                        <script>
                            setTimeout(function() {
                            window.location.href = '/login';
                            }, 3000);
                        </script>
                        </body>
                    </html>
                `);
            }
        } else {
            res.status(401).send(`
                    <html>
                        <body style="font-family: sans-serif; text-align: center; margin-top: 50px;">
                        <h2 style="color: red;">Invalid username or password</h2>
                        <p>Redirecting to login page in 3 seconds...</p>
                        <script>
                            setTimeout(function() {
                            window.location.href = '/login';
                            }, 3000);
                        </script>
                        </body>
                    </html>
                `);
        }
    } catch (err) {
        console.error(err);
        res.status(500).send('Server error');
    }
});

// Register handler
app.post('/register', validAccount, async (req, res) => {
    const { email, username, password } = req.body;

    try {
        await User.create({ email, username, password });
        res.status(200).send('Register successfully');
    } catch (err) {
        console.error(err);
        res.status(500).send('Server error');
    }
});

// Email-sending handler
app.post('/send-email', async (req, res) => {
    const { to, subject, text } = req.body;

    const deliver = nodemailer.createTransport({
        service: 'gmail',
        auth: {
            user: 'thinhpham2310@gmail.com',
            pass: 'yzaz atae hzvm yymn',
        },
        tls: { rejectUnauthorized: false },
    });

    const mailDetails = {
        from: 'thinhpham2310@gmail.com',
        to,
        subject,
        text,
    };

    try {
        await deliver.sendMail(mailDetails);
        res.status(200).send('Sent email succesfully');
    } catch (error) {
        console.error(error);
        res.status(500).send('Failed to send email');
    }
});

// Data-sending from ESP32
app.post('/data', async (req, res) => {
    let { time, temp, hum, gas, dust, level } = req.body;

    if (typeof time === 'string' && /^\d{2}:\d{2}:\d{2}$/.test(time)) {
        const today = new Date();
        const dateStr = today.toISOString().slice(0, 10); // yyyy-mm-dd
        time = new Date(`${dateStr}T${time}`);
    } else {
        time = new Date(time);
    }

    try {
        await Environment.create({
            time: time,
            temperature: temp,
            humidity: hum,
            dust_density: dust,
            gas_density: gas,
            level
        });
        console.log('Data received:', { time, temp, hum, gas, dust, level });
        clients.forEach(c =>
            c.write(`event: data-ready\ndata: ${JSON.stringify({
                time: time,
                temperature: temp,
                humidity: hum,
                dust_density: dust,
                gas_density: gas,
                level
            })}\n\n`)
        );

        res.json({ status: "Data received" });
    } catch (err) {
        console.error(err);
        res.status(500).send('Server error');
    }
});

// SSE Endpoint for Client web reciving real-time events
app.get('/events', (req, res) => {
    res.setHeader('Content-Type', 'text/event-stream');
    res.setHeader('Cache-Control', 'no-cache');
    res.setHeader('Connection', 'keep-alive');
    res.flushHeaders();

    clients.push(res);

    req.on('close', () => {
        clients = clients.filter(c => c !== res);
    });
});

// Get instant data handler
app.get('/get-instant-data', (req, res) => {
    console.log('Received instant data request');
    esp32Task = "send-data";
    res.json({ status: "Request sent to ESP32" });
});

// Change silent mode handler
app.get('/change-silent-mode', (req, res) => {
    console.log('Received change silent mode request');
    esp32Task = "change-silent-mode";
    res.json({ status: "Request sent to ESP32" });
});

// Test alert handler
app.get('/test-alert', (req, res) => {
    console.log('Received test alert request');
    esp32Task = "test-alert";
    res.json({ status: "Request sent to ESP32" });
});



// esp32 detects for tasks from web server
app.get('/check-task', (req, res) => {
    console.log('ESP32 checking for tasks, current task:', esp32Task);
    if (esp32Task) {
        res.json({ task: esp32Task });
        esp32Task = null;
    } else {
        res.json({ task: "none" });
    }
});

// Logout handler
app.get('/logout', (req, res) => {
    req.session.destroy(() => {
        res.redirect('/login');
    });
});

app.get('/chart-data', async (req, res) => {
    try {
        const data = await Environment.find().sort({ time: 1 }).exec();
        // console.log(data);
        res.json(data);
    } catch (err) {
        console.error(err);
        res.status(500).send('Server error');
    }
});

//interval config
let intervalConfig = { value: 5, unit: 'min' }; // default

app.get('/get-interval', (req, res) => {
    res.json(intervalConfig);
});

app.post('/set-interval', express.json(), (req, res) => {
    const { value, unit } = req.body;
    intervalConfig = { value, unit };
    res.json({ success: true });
});

app.get('/latest-data', async (req, res) => {
    try {
        const latest = await Environment.findOne().sort({ time: -1 }).exec();
        res.json(latest);
    } catch (err) {
        console.error(err);
        res.status(500).send('Server error');
    }
});



app.listen(PORT, "0.0.0.0", () => {
    console.log(`Running on port ${PORT}`);
});
