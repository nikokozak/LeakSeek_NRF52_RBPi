// LeakSeek Main Application
// Manages state, UI updates, and polling

// Configuration
const POLL_INTERVAL = 2000; // 2 seconds
const LEAK_THRESHOLD = 1; // Value indicating a leak

// State
const state = {
  registeredDevices: [],
  sensorData: {},
  discoveredDevices: [],
  currentScreen: 'main',
  selectedSensor: null,
  pollingInterval: null
};

// Debug console
const debug = {
  enabled: false,
  log: function(message, type = 'info') {
    if (this.enabled) {
      const output = document.getElementById('debug-output');
      const line = document.createElement('div');
      line.className = `debug-line ${type}`;
      line.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
      output.appendChild(line);
      output.scrollTop = output.scrollHeight;
    }
    console.log(message);
  }
};

// Initialize app
function initApp() {
  attachEventHandlers();
  startPolling();
  debug.log('App initialized', 'success');
}

// Attach event handlers
function attachEventHandlers() {
  // Main screen
  document.getElementById('menu-btn').addEventListener('click', showMenu);
  document.getElementById('discover-btn').addEventListener('click', () => navigateTo('discover'));
  
  // Discover screen
  document.getElementById('back-btn').addEventListener('click', () => navigateTo('main'));
  document.getElementById('refresh-btn').addEventListener('click', refreshDiscovered);
  
  // Detail screen
  document.getElementById('detail-back-btn').addEventListener('click', () => navigateTo('main'));
  document.getElementById('rename-sensor-btn').addEventListener('click', handleRenameSensor);
  document.getElementById('forget-sensor-btn').addEventListener('click', handleForgetSensor);
  
  // Menu
  document.getElementById('menu-discover').addEventListener('click', () => {
    hideMenu();
    navigateTo('discover');
  });
  document.getElementById('menu-debug').addEventListener('click', toggleDebug);
  document.getElementById('menu-close').addEventListener('click', hideMenu);
  document.getElementById('debug-close').addEventListener('click', toggleDebug);
}

// Navigation
function navigateTo(screen) {
  const screens = document.querySelectorAll('.screen');
  screens.forEach(s => s.classList.remove('active'));
  
  state.currentScreen = screen;
  
  switch(screen) {
    case 'main':
      document.getElementById('main-screen').classList.add('active');
      break;
    case 'discover':
      document.getElementById('discover-screen').classList.add('active');
      refreshDiscovered();
      break;
    case 'detail':
      document.getElementById('sensor-detail-screen').classList.add('active');
      renderSensorDetail();
      break;
  }
  
  debug.log(`Navigated to ${screen}`, 'info');
}

// Show/hide menu
function showMenu() {
  document.getElementById('menu-overlay').classList.add('active');
}

function hideMenu() {
  document.getElementById('menu-overlay').classList.remove('active');
}

// Toggle debug console
function toggleDebug() {
  debug.enabled = !debug.enabled;
  const console = document.getElementById('debug-console');
  console.classList.toggle('active');
  hideMenu();
}

// Start polling for sensor data
function startPolling() {
  if (state.pollingInterval) {
    clearInterval(state.pollingInterval);
  }
  
  // Initial fetch
  pollSensorData();
  
  // Set up interval
  state.pollingInterval = setInterval(pollSensorData, POLL_INTERVAL);
  debug.log('Polling started', 'success');
}

// Poll sensor data
async function pollSensorData() {
  try {
    // Fetch registered devices and sensor data
    const [devices, data] = await Promise.all([
      getRegisteredDevices(),
      getSensorData()
    ]);
    
    state.registeredDevices = devices;
    state.sensorData = data;
    
    renderSensorList();
    
  } catch (error) {
    debug.log(`Polling error: ${error.message}`, 'error');
  }
}

// Render sensor list on main screen
function renderSensorList() {
  const container = document.getElementById('sensor-list');
  const emptyState = document.getElementById('empty-state');
  
  if (state.registeredDevices.length === 0) {
    container.style.display = 'none';
    emptyState.style.display = 'flex';
    return;
  }
  
  container.style.display = 'block';
  emptyState.style.display = 'none';
  
  // Combine device info with sensor data
  const sensors = state.registeredDevices.map(device => {
    const data = state.sensorData[device.address] || {};
    return {
      ...device,
      value: data.value !== undefined ? data.value : null,
      timestamp: data.timestamp || null,
      hasLeak: data.value >= LEAK_THRESHOLD
    };
  });
  
  // Sort: leaks first, then by name
  sensors.sort((a, b) => {
    if (a.hasLeak && !b.hasLeak) return -1;
    if (!a.hasLeak && b.hasLeak) return 1;
    return a.name.localeCompare(b.name);
  });
  
  // Render
  container.innerHTML = sensors.map(sensor => `
    <div class="sensor-card ${sensor.hasLeak ? 'alert' : ''}" data-address="${sensor.address}">
      <div class="sensor-header">
        <div class="sensor-name">${sensor.name}</div>
        <div class="sensor-status">${sensor.value !== null ? sensor.value : '—'}</div>
      </div>
      <div class="sensor-meta">
        <span class="sensor-address">${sensor.address}</span>
        <span class="sensor-timestamp">${formatTimestamp(sensor.timestamp)}</span>
      </div>
    </div>
  `).join('');
  
  // Attach click handlers
  container.querySelectorAll('.sensor-card').forEach(card => {
    card.addEventListener('click', (e) => {
      const address = e.currentTarget.dataset.address;
      showSensorDetail(address);
    });
  });
}

// Show sensor detail screen
function showSensorDetail(address) {
  const sensor = state.registeredDevices.find(d => d.address === address);
  const data = state.sensorData[address] || {};
  
  if (!sensor) return;
  
  state.selectedSensor = {
    ...sensor,
    value: data.value !== undefined ? data.value : null,
    timestamp: data.timestamp || null,
    hasLeak: data.value >= LEAK_THRESHOLD
  };
  
  navigateTo('detail');
}

// Render sensor detail screen
function renderSensorDetail() {
  const sensor = state.selectedSensor;
  if (!sensor) return;
  
  document.getElementById('sensor-detail-name').textContent = sensor.name;
  document.getElementById('sensor-detail-address').textContent = sensor.address;
  
  const statusEl = document.getElementById('sensor-detail-status');
  statusEl.textContent = sensor.value !== null ? sensor.value : '—';
  statusEl.className = `status-value ${sensor.hasLeak ? 'alert' : ''}`;
  
  document.getElementById('sensor-detail-timestamp').textContent = 
    formatTimestamp(sensor.timestamp, true);
}

// Handle rename sensor
function handleRenameSensor() {
  if (!state.selectedSensor) return;
  
  showKeyboard(state.selectedSensor.name, async (newName) => {
    if (newName && newName !== state.selectedSensor.name) {
      try {
        debug.log(`Renaming ${state.selectedSensor.address} to ${newName}`, 'info');
        
        // Use the new rename endpoint
        await renameDevice(state.selectedSensor.address, newName);
        
        debug.log('Rename successful', 'success');
        
        // Refresh data
        await pollSensorData();
        navigateTo('main');
        
      } catch (error) {
        debug.log(`Rename failed: ${error.message}`, 'error');
      }
    }
  });
}

// Handle forget sensor
async function handleForgetSensor() {
  if (!state.selectedSensor) return;
  
  // Simple confirmation (you could add a modal here)
  const confirmed = confirm(`Forget sensor "${state.selectedSensor.name}"?`);
  if (!confirmed) return;
  
  try {
    debug.log(`Forgetting sensor ${state.selectedSensor.address}`, 'info');
    await unregisterDevice(state.selectedSensor.address);
    debug.log('Sensor forgotten', 'success');
    
    await pollSensorData();
    navigateTo('main');
    
  } catch (error) {
    debug.log(`Failed to forget sensor: ${error.message}`, 'error');
  }
}

// Refresh discovered devices
async function refreshDiscovered() {
  const emptyState = document.getElementById('discovering-state');
  const container = document.getElementById('discovered-list');
  
  try {
    // Show scanning state
    emptyState.innerHTML = '<p>Scanning for sensors...</p>';
    emptyState.style.display = 'flex';
    container.style.display = 'none';
    
    debug.log('Triggering BLE scan...', 'info');
    
    // Trigger an actual BLE scan (takes 5 seconds)
    const scanResult = await triggerScan(5.0);
    
    if (scanResult.status === 'busy') {
      debug.log('Scan already in progress, waiting...', 'info');
      // Wait a bit and try to fetch results
      await new Promise(resolve => setTimeout(resolve, 1000));
    } else if (scanResult.status === 'complete' || scanResult.status === 'timeout') {
      debug.log(`Scan complete: ${scanResult.devices_found || '?'} devices found`, 'info');
    } else {
      debug.log(`Scan status: ${scanResult.status}`, 'warn');
    }
    
    // Fetch both discovered AND registered to filter properly
    const [discovered, registered] = await Promise.all([
      getDiscoveredDevices(),
      getRegisteredDevices()
    ]);
    
    state.discoveredDevices = discovered;
    state.registeredDevices = registered;
    
    renderDiscoveredList();
    debug.log(`Displaying ${state.discoveredDevices.length} LeakSeek device(s)`, 'success');
  } catch (error) {
    debug.log(`Discovery error: ${error.message}`, 'error');
    emptyState.innerHTML = '<p>Scan failed - try again</p>';
  }
}

// Render discovered devices list
function renderDiscoveredList() {
  const container = document.getElementById('discovered-list');
  const emptyState = document.getElementById('discovering-state');
  
  if (state.discoveredDevices.length === 0) {
    container.style.display = 'none';
    emptyState.style.display = 'flex';
    return;
  }
  
  container.style.display = 'block';
  emptyState.style.display = 'none';
  
  // Filter out already registered devices
  const registeredAddresses = state.registeredDevices.map(d => d.address);
  const unregistered = state.discoveredDevices.filter(
    d => !registeredAddresses.includes(d.address)
  );
  
  container.innerHTML = unregistered.map(device => `
    <div class="discovered-card" data-address="${device.address}">
      <div class="discovered-info">
        <div class="discovered-name">${device.name}</div>
        <div class="discovered-address">${device.address}</div>
      </div>
      <button class="btn btn-primary register-btn">Add</button>
    </div>
  `).join('');
  
  // Attach click handlers to Add buttons
  container.querySelectorAll('.register-btn').forEach(btn => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      const card = e.target.closest('.discovered-card');
      const address = card.dataset.address;
      handleRegisterDevice(address);
    });
  });
}

// Handle register device
function handleRegisterDevice(address) {
  const device = state.discoveredDevices.find(d => d.address === address);
  if (!device) return;
  
  showKeyboard('', async (name) => {
    if (name) {
      try {
        debug.log(`Registering ${address} as "${name}"`, 'info');
        await registerDevice(address, name);
        debug.log('Registration successful', 'success');
        
        await pollSensorData();
        navigateTo('main');
        
      } catch (error) {
        debug.log(`Registration failed: ${error.message}`, 'error');
      }
    }
  });
}

// Format timestamp
function formatTimestamp(timestamp, full = false) {
  if (!timestamp) return '—';
  
  const date = new Date(timestamp * 1000);
  const now = new Date();
  const diff = Math.floor((now - date) / 1000);
  
  if (full) {
    return date.toLocaleString();
  }
  
  if (diff < 60) return `${diff}s ago`;
  if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
  if (diff < 86400) return `${Math.floor(diff / 3600)}h ago`;
  return date.toLocaleDateString();
}

// Initialize when DOM is ready
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', initApp);
} else {
  initApp();
}

