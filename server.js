const express = require('express');
const path = require('path');

const PORT = process.env.PORT || 3000;

const app = express();

// setup folder views: ejs
app.set("views", path.join(__dirname, "views"));
app.set("view engine", "ejs");

// setup folder public that contains all static files
app.use(express.static(path.join(__dirname, "public")));

app.get("/", (req, res) => {
    res.render("pages/home");
});

app.get("/chart", (req, res) => {
    res.render("pages/history-report");
});

app.get("/threshold", (req, res) => {
    res.render("pages/threshold-setting");
});

app.get("/scheduler", (req, res) => {
    res.render("pages/scheduler");
});

app.listen(PORT, () => {
    console.log(`Running on port ${PORT}`);
});