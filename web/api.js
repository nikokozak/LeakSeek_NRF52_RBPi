// API Configuration
// Use relative URLs so it works with Flask serving static files
const API_BASE = '';

// Simple fetch wrapper with error handling
async function apiFetch(endpoint, options = {}) {
  try {
    const response = await fetch(`${API_BASE}${endpoint}`, options);
    if (!response.ok) {
      throw new Error(`API Error: ${response.status}`);
    }
    return await response.json();
  } catch (error) {
    console.error(`Failed to fetch ${endpoint}:`, error);
    throw error;
  }
}

// Get all sensor data
// Returns: { address: { value: number, timestamp: number }, ... }
async function getSensorData() {
  return apiFetch('/sensor_data');
}

// Get sensor data for specific address
// Returns: { value: number, timestamp: number } or { error: string }
async function getSensorDataByAddress(address) {
  return apiFetch(`/sensor_data/${address}`);
}

// Get list of discovered (unregistered) devices
// Returns: [{ name: string, address: string }, ...]
async function getDiscoveredDevices() {
  return apiFetch('/discovered_devices');
}

// Get list of connected devices
// Returns: [{ name: string, address: string }, ...]
async function getConnectedDevices() {
  return apiFetch('/connected_devices');
}

// Get list of registered devices
// Returns: [{ name: string, address: string }, ...]
async function getRegisteredDevices() {
  return apiFetch('/registered_devices');
}

// Register a device with a name
// Returns: { status: string, address: string, name: string }
async function registerDevice(address, name) {
  return apiFetch(`/register/${address}?name=${encodeURIComponent(name)}`, {
    method: 'POST'
  });
}

// Unregister a device
// Returns: { status: string, address: string }
async function unregisterDevice(address) {
  return apiFetch(`/unregister/${address}`, {
    method: 'POST'
  });
}

// Unregister all devices
// Returns: { status: string }
async function unregisterAllDevices() {
  return apiFetch('/unregister_all', {
    method: 'POST'
  });
}

// Rename a device
// Returns: { status: string, address: string, new_name: string }
async function renameDevice(address, newName) {
  return apiFetch(`/rename/${address}?new_name=${encodeURIComponent(newName)}`, {
    method: 'POST'
  });
}

// Trigger a BLE scan
// Returns: { status: string, devices_found: number }
async function triggerScan(timeout = 5.0) {
  return apiFetch(`/scan?timeout=${timeout}`, {
    method: 'POST'
  });
}

