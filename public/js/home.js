const webServerIP = "192.168.56.67";
const webServerPORT = "3000"

const evtSource = new EventSource(`http://${webServerIP}:${webServerPORT}/events`);
console.log(`http://${webServerIP}:${webServerPORT}/events`);
console.log(`http://${webServerIP}:${webServerPORT}/get-instant-data`);

evtSource.addEventListener('data-ready', function(event) {
    const data = JSON.parse(event.data);
    console.log("Data ready from ESP32:", data);
    // Ở đây có thể: hiển thị UI, hoặc gọi API cloud để lấy bản đầy đủ
});

function requestInstantData() {
    fetch(`http://${webServerIP}:${webServerPORT}/get-instant-data`);
}

setInterval(requestInstantData, 15000);
requestInstantData();