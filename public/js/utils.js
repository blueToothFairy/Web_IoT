const webServerIP = "172.21.80.1";
const webServerPORT = "3000"

function requestInstantData() {
    fetch(`http://${webServerIP}:${webServerPORT}/get-instant-data`);
}

async function warningEmail(lvTemp, lvHumL, lvGas, lvDust, emailUser) {
    let text = "SafeSense360 Alert:\n\n";

    if (lvTemp === 1) {
        text += "Temperature is at WARNING level.\n";
    } else if (lvTemp === 2) {
        text += "Temperature is at DANGEROUS level!\n";
    }

    if (lvHumL === 1) {
        text += "Humidity is at WARNING level.\n";
    } else if (lvHumL === 2) {
        text += "Humidity is at DANGEROUS level!\n";
    }

    if (lvGas === 1) {
        text += "Gas concentration is at WARNING level.\n";
    } else if (lvGas === 2) {
        text += "Gas concentration is at DANGEROUS level!\n";
    }

    if (lvDust === 1) {
        text += "Dust density is at WARNING level.\n";
    } else if (lvDust === 2) {
        text += "Dust density is at DANGEROUS level!\n";
    }

    const email = {
        to: emailUser,
        subject: "SafeSense360 - Environmental Alert",
        text
    };

    const res = await fetch(`http://${webServerIP}:${webServerPORT}/send-email`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
            to: emailUser,
            subject: "Warning from SafeSense360",
            text: text
        })
    });
    if (res.ok) {
        console.log(`Sent warning email to ${email}`);
    }
}

module.exports = {
    requestInstantData,
    warningEmail,
    webServerIP,
    webServerPORT
};