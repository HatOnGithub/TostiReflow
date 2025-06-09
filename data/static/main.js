const MonitorButton = document.getElementById('monitor');
const MonitorContent = document.getElementById('monitor-content');
const SettingsButton = document.getElementById('settings');
const SettingsContent = document.getElementById('settings-content');
const HelpButton = document.getElementById('help');
const HelpContent = document.getElementById('help-content');
const AboutButton = document.getElementById('about');
const AboutContent = document.getElementById('about-content');
const LastStatusTime = document.getElementById('last-updated');

var lastState;

// Add event listeners to the buttons
MonitorButton.addEventListener('click', () => showContent('monitor'));
SettingsButton.addEventListener('click', () => showContent('settings'));
HelpButton.addEventListener('click', () => showContent('help'));
AboutButton.addEventListener('click', () => showContent('about'));

init();

setInterval(refresStatus, 500);

function init() {
    // Show the monitor content by default
    showContent('monitor');
 
    // Update the profiles dropdown
    updateProfiles();

    // Fetch initial data for the monitor
    refresStatus();

    // Set up a timer to refresh the monitor data every 5 seconds
    setInterval(refresStatus, 5000);
}

function updateProfiles(){
    fetch('/profiles')
        .then(response => response.json())
        .then(data => {
            console.log('Retrieved profiles:', data);
            const profileSelect = document.getElementById('profile-select');
            profileSelect.innerHTML = data; // Clear existing options
            data.forEach(profile => {
                const option = document.createElement('option');
                option.value = profile;
                option.textContent = profile;
                profileSelect.appendChild(option);
            });
        })
        .catch(error => {
            console.error('Error loading profiles:', error);
        });
    
}

// Fetch new data from the ESP32
function refresStatus() {
    fetch('/status')
        .then(response => response.json())
        .then(data => {
            lastState = data;
            console.log('Last status data:', data);
            LastStatusTime.textContent = `${new Date().toLocaleTimeString()}`;
        })
        .catch(error => {
            console.error('Error fetching monitor data:', error);
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