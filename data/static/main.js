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

init();

function init() {
    // Show the monitor content by default
    showContent('monitor');
 
    // Update the profiles dropdown
    updateProfiles();

    // Fetch initial data for the monitor
    refreshMonitor();

    // Set up a timer to refresh the monitor data every 5 seconds
    setInterval(refreshMonitor, 5000);
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
function refreshMonitor() {
    fetch('/status')
        .then(response => response.json())
        .then(data => {
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