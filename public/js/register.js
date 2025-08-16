document.getElementById('register-form').addEventListener("submit", async (e) => {
    e.preventDefault();
    const email = document.getElementById('email').value;
    const username = document.getElementById('username').value;
    const pass1 = document.getElementById('password').value;
    const pass2 = document.getElementById('confirm-password').value;
    const error = document.getElementById('error');

    if (pass1 != pass2) {
        error.innerText = "Passwords do not match. Try again";
        return;
    }

    try {
        const res1 = await fetch("/register", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                email: email,
                username: username,
                password: pass1,
            })
        });
        const msg1 = await res1.text();


        const res2 = await fetch('/send-email', {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                to: email,
                subject: "Greeting from SafeSense360",
                text: "Welcome to SafeSense360 – Breathe Safe, Live Well!\n\nAt SafeSense360, we’re committed to safeguarding your health by delivering precise, real-time air quality insights. Our advanced monitoring solutions empower you to make informed decisions, ensuring a cleaner, safer environment for you and your loved ones.\nThank you for trusting us to protect what matters most—your well-being. Explore our innovative technology and take the first step toward healthier air today!\n\nStay informed. Stay protected. SafeSense360."
            })
        });
        const msg2 = await res2.text();
        localStorage.setItem('res1Sta', res1.status);
        localStorage.setItem('res2Sta', res2.status);

        if (res1.ok) {
            localStorage.setItem('res1', 1);
            window.location.href = "/login";
        } else {
            localStorage.setItem('res1', 0);
            error.innerText = msg1.error || "An error occurred. Please try again.";
        }

        if (!res2.ok) {
            localStorage.setItem('res2', 0);
            error.innerText = msg1.error || "An error occurred. Please try again.";
        }

    } catch (err) {
        error.innerText = "Cannot connect to server.";
    }
});