// Simple on-screen keyboard for touch input
// Provides a basic QWERTY layout with limited character set

const KEYBOARD_LAYOUT = [
  ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0'],
  ['Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'],
  ['A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', '-'],
  ['Z', 'X', 'C', 'V', 'B', 'N', 'M', '_', '⌫', '⌫']
];

let keyboardCallback = null;
let keyboardInput = null;
let keyboardOverlay = null;
let keyboard = null;

// Initialize keyboard on page load
function initKeyboard() {
  keyboardInput = document.getElementById('keyboard-input');
  keyboardOverlay = document.getElementById('keyboard-overlay');
  keyboard = document.getElementById('keyboard');
  
  renderKeyboard();
  attachKeyboardHandlers();
}

// Render keyboard keys
function renderKeyboard() {
  keyboard.innerHTML = '';
  
  KEYBOARD_LAYOUT.forEach(row => {
    row.forEach(key => {
      const keyBtn = document.createElement('button');
      keyBtn.className = 'key';
      keyBtn.textContent = key;
      keyBtn.dataset.key = key;
      
      if (key === '⌫') {
        keyBtn.className += ' backspace';
      }
      
      keyBtn.addEventListener('click', () => handleKeyPress(key));
      keyboard.appendChild(keyBtn);
    });
  });
}

// Handle key press
function handleKeyPress(key) {
  const currentValue = keyboardInput.value;
  
  if (key === '⌫') {
    keyboardInput.value = currentValue.slice(0, -1);
  } else if (currentValue.length < 16) { // Character limit
    keyboardInput.value = currentValue + key;
  }
}

// Attach event handlers for keyboard actions
function attachKeyboardHandlers() {
  document.getElementById('keyboard-clear').addEventListener('click', () => {
    keyboardInput.value = '';
  });
  
  document.getElementById('keyboard-cancel').addEventListener('click', () => {
    hideKeyboard();
  });
  
  document.getElementById('keyboard-submit').addEventListener('click', () => {
    const value = keyboardInput.value.trim();
    if (value && keyboardCallback) {
      keyboardCallback(value);
    }
    hideKeyboard();
  });
}

// Show keyboard with optional initial value and callback
function showKeyboard(initialValue = '', callback = null) {
  keyboardInput.value = initialValue;
  keyboardCallback = callback;
  keyboardOverlay.classList.add('active');
  keyboardInput.focus();
}

// Hide keyboard
function hideKeyboard() {
  keyboardOverlay.classList.remove('active');
  keyboardInput.value = '';
  keyboardCallback = null;
}

// Initialize when DOM is ready
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', initKeyboard);
} else {
  initKeyboard();
}

