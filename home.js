function updateClock() {
    const now = new Date();

    // Format time
    let hours = now.getHours().toString().padStart(2, '0');
    let minutes = now.getMinutes().toString().padStart(2, '0');
    let seconds = now.getSeconds().toString().padStart(2, '0');

    // Format date
    let day = now.getDate().toString().padStart(2, '0');
    let month = (now.getMonth() + 1).toString().padStart(2, '0');
    let year = now.getFullYear();

    document.getElementById("clock").textContent = `${hours}:${minutes}:${seconds}`;
    document.getElementById("date").textContent = `${month}/${day}/${year}`;
}

// Run clock immediately and update every second
setInterval(updateClock, 1000);
updateClock();