const express = require('express');
const mongoose = require('mongoose');
const path = require('path');

const PORT = process.env.PORT || 3000;
const app = express();

// Middleware
app.use(express.urlencoded({ extended: true }));
app.use(express.json());

// Connect to MongoDB
mongoose.connect('mongodb+srv://vnbtram23:g6yKiJiOXicvmGvB@cluster0.qfxfbpd.mongodb.net/SafeSense360')
    .then(() => console.log("MongoDB connected"))
    .catch(err => console.error(err));

// Models
const userSchema = new mongoose.Schema({
    username: { type: String, required: true },
    password: { type: String, required: true }
}, { collection: "User" });
const User = mongoose.model('User', userSchema);

const Environment = mongoose.model('Environment', {
    time: Date,
    temperature: Number,
    humidity: Number,
    dust_density: Number,
    gas_density: Number
}, "Environment");

// EJS view setup
app.set("views", path.join(__dirname, "views"));
app.set("view engine", "ejs");

// Static folder
app.use(express.static(path.join(__dirname, "public")));

// Pages
app.get("/", (req, res) => res.render("pages/home"));
app.get("/chart", (req, res) => res.render("pages/history-report"));
app.get("/threshold", (req, res) => res.render("pages/threshold-setting"));
app.get("/scheduler", (req, res) => res.render("pages/scheduler"));
app.get("/login", (req, res) => res.render("pages/login"));
app.get("/sign-up", (req, res) => res.render("pages/sign-up"));

// Login handler
app.post('/login', async (req, res) => {
    console.log(req.body);
    const { username, password } = req.body;

    try {
        const user = await User.findOne({ username, password });
        if (user) {
            res.redirect('/');
        } else {
            res.status(401).send('Invalid username or password');
        }
    } catch (err) {
        console.error(err);
        res.status(500).send('Server error');
    }
});

app.listen(PORT, () => {
    console.log(`Running on port ${PORT}`);
});
