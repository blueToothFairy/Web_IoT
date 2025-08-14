const webServerIP = "192.168.56.67";
const webServerPORT = "3000"

const evtSource = new EventSource(`http://${webServerIP}:${webServerPORT}/events`);
console.log(`http://${webServerIP}:${webServerPORT}/events`);
console.log(`http://${webServerIP}:${webServerPORT}/get-instant-data`);

evtSource.addEventListener('data-ready', function (event) {
    updateStatusCircle();
});

function requestInstantData() {
    fetch(`http://${webServerIP}:${webServerPORT}/get-instant-data`);
}

function changeSilentMode() {
    fetch(`http://${webServerIP}:${webServerPORT}/change-silent-mode`);
}

function testAlert() {
    fetch(`http://${webServerIP}:${webServerPORT}/test-alert`);
}

setInterval(requestInstantData, 15000);
requestInstantData();
// Silent Mode
const silentModeToggle = document.getElementById("silent-mode-toggle");
silentModeToggle.addEventListener("change", (event) => {
    // const isChecked = event.target.checked;
    changeSilentMode();
});

// Detail
const detailToggle = document.getElementById("detail-toggle");
const detailContent = document.getElementById("detail-content");

detailToggle.addEventListener("click", async () => {
    const isVisible = detailContent.style.display === "block";
    if (isVisible) {
        detailContent.style.display = "none";
        detailToggle.querySelector(".arrow").innerHTML = "&#9660;"; // down arrow
    } else {
        // Fetch latest data
        try {
            const res = await fetch('/latest-data');
            const data = await res.json();

            document.getElementById("temp-value").textContent = data.temperature ?? "--";
            document.getElementById("humidity-value").textContent = data.humidity ?? "--";
            document.getElementById("dust-value").textContent = data.dust_density ?? "--";
            document.getElementById("gas-value").textContent = data.gas_density ?? "--";
            console.log("Detail data fetched successfully:", data);
        } catch (err) {
            console.error("Failed to fetch detail data:", err);
        }

        detailContent.style.display = "block";
        detailToggle.querySelector(".arrow").innerHTML = "&#9650;"; // up arrow
    }
});
// Keep latest interval so other pages (like scheduler) can use it
let latestInterval = { value: 5, unit: 'min' };

function updateClock() {
    const now = new Date();
    document.getElementById("clock").textContent = now.toLocaleTimeString();
    document.getElementById("date").textContent = now.toLocaleDateString();
}

async function updateStatusCircle() {
    try {
        const res = await fetch('/latest-data');
        const data = await res.json();
        const circle = document.querySelector('.status-circle');

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
    } catch (error) {
        console.error('Error fetching latest data:', error);
    }
}

async function getIntervalFromServer() {
    try {
        const res = await fetch('/get-interval');
        const data = await res.json();
        latestInterval = data; // store globally so other pages can use
        return data;
    } catch (err) {
        console.error('Failed to get interval:', err);
        return latestInterval; // fallback
    }
}

async function startDataFetchLoop() {
    const { value, unit } = await getIntervalFromServer();
    let intervalMs = value * (unit === 'hour' ? 3600000 : 60000);

    console.log(`Fetching data every ${value} ${unit}`);

    // Initial fetch
    updateStatusCircle();

    // Repeated fetch
    setInterval(() => {
        updateStatusCircle();
    }, intervalMs);
}

// Clock updates
setInterval(updateClock, 1000);
updateClock();

// Start fetching
document.addEventListener('DOMContentLoaded', startDataFetchLoop);