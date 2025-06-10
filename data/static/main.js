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
var lastProfile; // this is to check if the profile was modified, aka unsaved changes

// Add event listeners to the buttons
MonitorButton.addEventListener('click', () => showContent('monitor'));
SettingsButton.addEventListener('click', () => showContent('settings'));
HelpButton.addEventListener('click', () => showContent('help'));
AboutButton.addEventListener('click', () => showContent('about'));

init();

// Set up a timer to refresh the monitor data every second
setInterval(refreshStatus, 1000);

function init() {
    // Show the monitor content by default
    showContent('monitor');
 
    // Update the profiles dropdown
    updateProfiles();

    // Fetch initial data for the monitor
    refreshStatus(true);
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

function loadProfile(){
    const profileSelect = document.getElementById('profile-select');
    const selectedProfile = profileSelect.value;

    if (!selectedProfile) {
        console.warn('No profile selected.');
        return;
    }

    fetch('/loadprofile', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({ name: selectedProfile })
    })
    .then(response => {
        console.log(response);
        lastProfile = selectedProfile; // Update lastProfile to the newly loaded profile
        refreshStatus(true); // Refresh status after loading profile
    })

}

// Fetch new data from the ESP32
function refreshStatus(updateProfileValues = false) {
    fetch('/status')
        .then(response => response.json())
        .then(data => {
            lastState = data;
            LastStatusTime.textContent = `${new Date().toLocaleTimeString()}`;
            displayStatus();
            if (updateProfileValues)
                changeValues();
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

function startReflow(){
    if (lastState.start === true) {
        console.warn('Reflow is already running.');
        return;
    }


    fetch('/start')
        .then(response => response.json())
        .then(data => {
            console.log('Reflow started:', data);
            refreshStatus();
        })
        .catch(error => {
            console.error('Error starting reflow:', error);
        })
        .finally(() => {
            refreshStatus();
        });
        
}

function stopReflow(){

    if (lastState.start === false) {
        console.warn('Reflow is not running.');
        return;
    }

    fetch('/stop')
        .then(response => response.json())
        .then(data => {
            console.log('Reflow stopped:', data);
            refreshStatus();
        })
        .catch(error => {
            console.error('Error stopping reflow:', error);
        })
        .finally(() => {
            refreshStatus();
        });
}

function displayStatus(){
    if (!lastState) {
        console.warn('No status data available.');
        return;
    }

    const tempratureDisplay = document.getElementById('temperature-display');
    tempratureDisplay.innerHTML = `
        <h3>Temperature</h3>
        <p>Current Temperature:\t${lastState.lastTemperature} °C (Note: Measures 20°C minimum)</p>
        <p>Target Temperature:\t${lastState.setpoint} °C</p>
        <p>PID Output:\t${lastState.pidOutput.toFixed(2)}</p>
        <p>Heater: ${lastState.pidOutput > 0.5 ? 'On' : 'Off'}</p>
    `;

    const statusDisplay = document.getElementById('status-display');

    var reflowStatus;

    if (lastState.start === false) 
        reflowStatus = 'Idle';
    else if (lastState.preheating)
        reflowStatus = 'Preheating';
    else if (lastState.soaking)
        reflowStatus = 'Soaking';
    else if (lastState.reflowing)
        reflowStatus = 'Reflowing';
    else if (lastState.coolingDown)
        reflowStatus = 'Cooling down';
    else
        reflowStatus = 'Error: Unknown state';


    statusDisplay.innerHTML = `
        <h3>Status</h3>
        <p>Reflow Status: ${reflowStatus}</p>
        `;

    const timeDisplay = document.getElementById('time-display');
    timeDisplay.innerHTML = `
        <h3>Time</h3>
        <p>${lastState.time}</p>
    `;
}

function changeValues(){
    const preheatTempInput = document.getElementById('preheat-temp');
    const preheatTimeInput = document.getElementById('preheat-time');
    const soakTempInput = document.getElementById('soak-temp');
    const soakTimeInput = document.getElementById('soak-time');
    const reflowTempInput = document.getElementById('reflow-temp');
    const reflowTimeInput = document.getElementById('reflow-time');
    const coolTempInput = document.getElementById('cooling-temp');
    const coolTimeInput = document.getElementById('cooling-time');

    preheatTempInput.value = parseFloat(lastState.preheatTemp);
    preheatTimeInput.value = parseInt(lastState.preheatTime);
    soakTempInput.value = parseFloat(lastState.soakTemp);
    soakTimeInput.value = parseInt(lastState.soakTime);
    reflowTempInput.value = parseFloat(lastState.reflowTemp);
    reflowTimeInput.value = parseInt(lastState.reflowTime);
    coolTempInput.value = parseFloat(lastState.cooldownTemp);
    coolTimeInput.value = parseInt(lastState.cooldownTime);
}

function sendValues(){
    
    // send the current settings to the server to save the profile
    const preheatTemp = parseFloat(document.getElementById('preheat-temp').value);
    const preheatTime = parseInt(document.getElementById('preheat-time').value);
    const soakTemp = parseFloat(document.getElementById('soak-temp').value);
    const soakTime = parseInt(document.getElementById('soak-time').value);
    const reflowTemp = parseFloat(document.getElementById('reflow-temp').value);
    const reflowTime = parseInt(document.getElementById('reflow-time').value);
    const coolTemp = parseFloat(document.getElementById('cooling-temp').value);
    const coolTime = parseInt(document.getElementById('cooling-time').value);

    if (isNaN(preheatTemp) || isNaN(preheatTime) || isNaN(soakTemp) || isNaN(soakTime) ||
        isNaN(reflowTemp) || isNaN(reflowTime) || isNaN(coolTemp) || isNaN(coolTime)) {
        alert('Please enter valid numeric values for all fields.');
        return;
    }

    // Prepare the data to be sent
    const profileData = {
        preheatTemp: preheatTemp,
        preheatTime: preheatTime,
        soakTemp: soakTemp,
        soakTime: soakTime,
        reflowTemp: reflowTemp,
        reflowTime: reflowTime,
        cooldownTemp: coolTemp,
        cooldownTime: coolTime
    };

    // Send the data to the server
    fetch('/setvalues', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(profileData)
    })
}

function saveProfile() {
    var profileName = document.getElementById('profile-name').value;
    if (!profileName) {
        alert('Please enter a profile name.');
        return;
    }

    // Send the profile data to the server
    sendValues();

    profileName += `.json`;

    fetch('/saveprofile', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({name: profileName})
    })
    .then(response => {
        console.log('Profile saved:', response);
        updateProfiles();

        lastProfile = profileName; // Update lastProfile to the newly saved profile
    })
    .catch(error => {
        console.error('Error saving profile:', error);
    });
}

function deleteProfile() {
    const profileSelect = document.getElementById('profile-select');
    const selectedProfile = profileSelect.value;

    if (!selectedProfile) {
        alert('Please select a profile to delete.');
        return;
    }

    fetch('/deleteprofile', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({ name: selectedProfile })
    })
    .then(response => response.json())
    .then(data => {
        console.log('Profile deleted:', data);
        updateProfiles();
        if (lastProfile === selectedProfile) {
            lastProfile = null; // Reset lastProfile if the deleted profile was the current one
        }
    
    })
    .catch(error => {
        console.error('Error deleting profile:', error);
    });
}