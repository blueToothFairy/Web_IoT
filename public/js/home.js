// Establish a connection to the server for real-time events
const evtSource = new EventSource(`http://${webServerIP}:${webServerPORT}/events`);
console.log(`http://${webServerIP}:${webServerPORT}/events`);
console.log(`http://${webServerIP}:${webServerPORT}/get-instant-data`);

evtSource.addEventListener('data-ready', function (event) {
    console.log("New data received from server via SSE.");
    updateDashboard();
});

function requestInstantData() {
    fetch(`http://${webServerIP}:${webServerPORT}/get-instant-data`);
}

function changeSilentMode() {
    fetch(`http://${webServerIP}:${webServerPORT}/change-silent-mode`);
}

function testAlert() {
    console.log('Testing alert...');
    fetch(`http://${webServerIP}:${webServerPORT}/test-alert`);
}

// --- DOM Element Event Listeners ---

// Silent Mode Toggle
const silentModeToggle = document.getElementById("silent-mode-toggle");
silentModeToggle.addEventListener("change", (event) => {
    changeSilentMode();
});

// Detail Section Toggle
const detailToggle = document.getElementById("detail-toggle");
const detailContent = document.getElementById("detail-content");

detailToggle.addEventListener("click", () => {
    const isVisible = detailContent.style.display === "block";
    if (isVisible) {
        detailContent.style.display = "none";
        detailToggle.querySelector(".arrow").innerHTML = "&#9660;"; // down arrow
    } else {
        // The content is already being updated in the background. Just show it.
        detailContent.style.display = "block";
        detailToggle.querySelector(".arrow").innerHTML = "&#9650;"; // up arrow
    }
});

// --- Core Functions ---

// Global variable to hold the latest interval settings for other pages to use
let latestInterval = { value: 5, unit: 'min' };

/*
 * Updates the digital clock and date display every second.
 */
function updateClock() {
    const now = new Date();
    document.getElementById("clock").textContent = now.toLocaleTimeString();
    document.getElementById("date").textContent = now.toLocaleDateString();
}

/**
 * Fetches the latest data and updates all relevant UI components in parallel.
 * This includes the main status circle and the detailed sensor readings.
 */
async function updateDashboard() {
    try {
        const res = await fetch('/latest-data');
        if (!res.ok) {
            throw new Error(`HTTP error! status: ${res.status}`);
        }
        const data = await res.json();
        const circle = document.querySelector('.status-circle');

        // Update Status Circle based on the safety level
        if (data && typeof data.level === 'number') {
            if (data.level === 0) {
                circle.textContent = 'Safe';
                circle.style.backgroundColor = 'green';
                circle.style.color = 'white';
            } else if (data.level === 1) {
                circle.textContent = 'Warning';
                circle.style.backgroundColor = 'yellow';
                circle.style.color = 'black';
            } else if (data.level === 2) {
                circle.textContent = 'Danger';
                circle.style.backgroundColor = 'red';
                circle.style.color = 'white';
            }
        }

        // Update Detail Content in parallel
        document.getElementById("temp-value").textContent = data.temperature ?? "--";
        document.getElementById("humidity-value").textContent = data.humidity ?? "--";
        document.getElementById("dust-value").textContent = data.dust_density ?? "--";
        document.getElementById("gas-value").textContent = data.gas_density ?? "--";

        console.log("Dashboard updated with latest data:", data);

    } catch (error) {
        console.error('Error fetching latest data to update dashboard:', error);
    }
}

/**
 * Fetches the data polling interval from the server.
 * @returns {Promise<object>} A promise that resolves to the interval configuration.
 */
async function getIntervalFromServer() {
    try {
        const res = await fetch('/get-interval');
        const data = await res.json();
        latestInterval = data; // store globally so other pages can use
        return data;
    } catch (err) {
        console.error('Failed to get interval:', err);
        return latestInterval; // fallback to default if fetch fails
    }
}

/**
 * Initializes the periodic data fetching loop.
 */
async function startDataFetchLoop() {
    const { value, unit } = await getIntervalFromServer();
    let intervalMs = value * (unit === 'hour' ? 3600000 : 60000);

    console.log(`Setting up data fetch every ${value} ${unit} (${intervalMs}ms)`);

    // Initial fetch to populate data on page load
    updateDashboard();

    // Set up repeated fetching based on the interval from the server
    setInterval(() => {
        updateDashboard();
    }, intervalMs);
}

// --- Initialization ---

// Start the clock as soon as the script runs
setInterval(updateClock, 1000);
updateClock();

// Start the main data fetching loop once the DOM is fully loaded
document.addEventListener('DOMContentLoaded', startDataFetchLoop);