const MonitorButton = document.getElementById('monitor');
const MonitorContent = document.getElementById('monitor-content');
const SettingsButton = document.getElementById('settings');
const SettingsContent = document.getElementById('settings-content');
const HelpButton = document.getElementById('help');
const HelpContent = document.getElementById('help-content');
const AboutButton = document.getElementById('about');
const AboutContent = document.getElementById('about-content');

// Add event listeners to the buttons
MonitorButton.addEventListener('click', () => showContent('monitor'));
SettingsButton.addEventListener('click', () => showContent('settings'));
HelpButton.addEventListener('click', () => showContent('help'));
AboutButton.addEventListener('click', () => showContent('about'));

// Fetch new data from the ESP32
function refreshMonitor() {
    fetch('/api/monitor')
        .then(response => response.json())
        .then(data => {
            // Update the monitor content with the new data
            MonitorContent.innerHTML = `
                <h2>Monitor Data</h2>
                <p>Temperature: ${data.temperature} °C</p>
                <p>Humidity: ${data.humidity} %</p>
                <p>Pressure: ${data.pressure} hPa</p>
            `;
        })
        .catch(error => {
            console.error('Error fetching monitor data:', error);
            MonitorContent.innerHTML = '<p>Error fetching data. Please try again later.</p>';
        });
}

function showContent(contentId) {
    // Hide all content sections
    MonitorContent.style.display = 'none';
    SettingsContent.style.display = 'none';
    HelpContent.style.display = 'none';
    AboutContent.style.display = 'none';

    // Show the selected content section
    switch (contentId) {
        case 'monitor':
            MonitorContent.style.display = 'block';
            break;
        case 'settings':
            SettingsContent.style.display = 'block';
            break;
        case 'help':
            HelpContent.style.display = 'block';
            break;
        case 'about':
            AboutContent.style.display = 'block';
            break;
    }
}