const express = require('express');
const mongoose = require('mongoose');
const path = require('path');
const session = require('express-session');
const bcrypt = require('bcrypt');

const PORT = process.env.PORT || 3000;
const app = express();

// Middleware
app.use(express.urlencoded({ extended: true }));
app.use(express.json());
app.use(session({
    secret: 'secretsafekey123',
    resave: false,
    saveUninitialized: false
}));

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
    gas_density: Number
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

// Logout handler
app.get('/logout', (req, res) => {
    req.session.destroy(() => {
        res.redirect('/login');
    });
});

app.get('/chart-data', async (req, res) => {
    try {
        const data = await Environment.find().sort({ time: -1 }).limit(100).exec();
        console.log(data);
        res.json(data);
    } catch (err) {
        console.error(err);
        res.status(500).send('Server error');
    }
});

app.listen(PORT, () => {
    console.log(`Running on port ${PORT}`);
});
