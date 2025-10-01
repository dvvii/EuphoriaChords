// Euphoria P5.js Interface for Bela
// iPhone Horizontal Format (844x390)

let hexPads = [];
let trillBars = [];
let activeHex = null;
let activeTrill = [];

function setup() {
  createCanvas(844, 390);
  
  // Initialize hex pads (left hand - root note selection)
  // Positioned in isometric hex layout
  hexPads = [
    { id: 7, note: 7, x: 140, y: 80, label: '7' },
    { id: 6, note: 6, x: 200, y: 80, label: '6' },
    { id: 3, note: 3, x: 110, y: 145, label: '3' },
    { id: 4, note: 4, x: 170, y: 145, label: '4' },
    { id: 2, note: 2, x: 140, y: 210, label: '2' },
    { id: 5, note: 5, x: 110, y: 275, label: '5' },
    { id: 1, note: 1, x: 140, y: 340, label: '1' }
  ];
  
  // Initialize trill bars (right hand - chord quality)
  // 4 bars, each with 2 zones (top/bottom)
  for (let i = 0; i < 4; i++) {
    trillBars.push({
      id: i + 1,
      x: 520 + (i * 70),
      y: 100,
      width: 50,
      height: 240,
      zones: [
        { id: 0, yStart: 0, yEnd: 0.5, active: false },    // Top zone
        { id: 1, yStart: 0.5, yEnd: 1.0, active: false }   // Bottom zone
      ],
      labels: ['1st', '2nd', '3rd', '4th']
    });
  }
  
  textAlign(CENTER, CENTER);
  textSize(16);
}

function draw() {
  background(240);
  
  // Title
  fill(0);
  textSize(20);
  textStyle(BOLD);
  text('EUPHORIA CONTROL', width/2, 30);
  
  // Section labels
  textSize(14);
  textStyle(NORMAL);
  fill(100);
  text('LEFT HAND - Root Note', 140, 60);
  text('RIGHT HAND - Chord Quality', 650, 60);
  
  // Draw hex pads
  drawHexPads();
  
  // Draw trill bars
  drawTrillBars();
  
  // Draw status messages
  drawStatus();
}

function drawHexPads() {
  for (let pad of hexPads) {
    let isActive = activeHex === pad.id;
    let d = dist(mouseX, mouseY, pad.x, pad.y);
    let isHovered = d < 30;
    
    // Hex body
    push();
    translate(pad.x, pad.y);
    
    if (isActive) {
      fill(255, 165, 0);
      stroke(255, 140, 0);
    } else if (isHovered) {
      fill(255, 200, 100);
      stroke(200, 150, 50);
    } else {
      fill(255);
      stroke(150);
    }
    
    strokeWeight(3);
    drawHexagon(0, 0, 30);
    
    // Label
    fill(isActive ? 255 : 50);
    noStroke();
    textSize(18);
    textStyle(BOLD);
    text(pad.label, 0, 0);
    
    pop();
  }
}

function drawHexagon(x, y, radius) {
  beginShape();
  for (let i = 0; i < 6; i++) {
    let angle = TWO_PI / 6 * i - HALF_PI;
    let vx = x + cos(angle) * radius;
    let vy = y + sin(angle) * radius;
    vertex(vx, vy);
  }
  endShape(CLOSE);
}

function drawTrillBars() {
  for (let i = 0; i < trillBars.length; i++) {
    let bar = trillBars[i];
    
    // Bar label
    fill(100);
    noStroke();
    textSize(12);
    text(bar.labels[i], bar.x + bar.width/2, bar.y - 15);
    
    // Draw each zone in the bar
    for (let j = 0; j < bar.zones.length; j++) {
      let zone = bar.zones[j];
      let zoneY = bar.y + (zone.yStart * bar.height);
      let zoneHeight = bar.height * (zone.yEnd - zone.yStart);
      
      // Check if mouse is over this zone
      let isHovered = mouseX >= bar.x && 
                      mouseX <= bar.x + bar.width &&
                      mouseY >= zoneY && 
                      mouseY <= zoneY + zoneHeight;
      
      // Color based on state
      if (zone.active) {
        fill(138, 43, 226); // Purple when active
        stroke(100, 20, 180);
      } else if (isHovered) {
        fill(200, 150, 255);
        stroke(150, 100, 200);
      } else {
        // Alternate colors for zones
        if (j === 0) {
          fill(100, 200, 255); // Light blue top
        } else {
          fill(255, 150, 200); // Pink bottom
        }
        stroke(150);
      }
      
      strokeWeight(2);
      rect(bar.x, zoneY, bar.width, zoneHeight, 5);
      
      // Zone indicator
      fill(255, 200);
      noStroke();
      textSize(10);
      text(j === 0 ? '♪' : '♫', bar.x + bar.width/2, zoneY + zoneHeight/2);
    }
  }
}

function drawStatus() {
  // Status box
  fill(50);
  noStroke();
  textAlign(LEFT, TOP);
  textSize(11);
  
  let statusY = height - 80;
  text('Last Messages:', 20, statusY);
  
  textSize(10);
  fill(0, 150, 0);
  
  if (activeHex !== null) {
    let pad = hexPads.find(p => p.id === activeHex);
    text('Hex: /euphoria/hex/' + pad.note + ' 1.0', 20, statusY + 20);
  } else {
    fill(150);
    text('Hex: (touch a hex pad)', 20, statusY + 20);
  }
  
  fill(138, 43, 226);
  if (activeTrill.length > 0) {
    text('Trill: ' + activeTrill.join(', '), 20, statusY + 35);
  } else {
    fill(150);
    text('Trill: (touch a bar)', 20, statusY + 35);
  }
  
  // Instructions
  fill(100);
  textSize(9);
  textAlign(RIGHT, TOP);
  text('Touch hex pads (left) and trill bars (right) to send control messages', width - 20, statusY);
}

function mousePressed() {
  handleTouch(mouseX, mouseY);
}

function touchStarted() {
  for (let i = 0; i < touches.length; i++) {
    handleTouch(touches[i].x, touches[i].y);
  }
  return false;
}

function handleTouch(x, y) {
  // Check hex pads
  for (let pad of hexPads) {
    let d = dist(x, y, pad.x, pad.y);
    if (d < 30) {
      activeHex = pad.id;
      sendHexMessage(pad.note);
      return;
    }
  }
  
  // Check trill bars
  for (let bar of trillBars) {
    if (x >= bar.x && x <= bar.x + bar.width &&
        y >= bar.y && y <= bar.y + bar.height) {
      
      // Determine which zone was touched
      for (let j = 0; j < bar.zones.length; j++) {
        let zone = bar.zones[j];
        let zoneY = bar.y + (zone.yStart * bar.height);
        let zoneHeight = bar.height * (zone.yEnd - zone.yStart);
        
        if (y >= zoneY && y <= zoneY + zoneHeight) {
          // Toggle zone
          zone.active = !zone.active;
          updateTrillState();
          
          // Calculate normalized position within zone (0.0 to 1.0)
          let posInZone = (y - zoneY) / zoneHeight;
          sendTrillMessage(bar.id, j, zone.active ? posInZone : 0.0);
          return;
        }
      }
    }
  }
}

function mouseReleased() {
  // On release, send off message for hex
  if (activeHex !== null) {
    let pad = hexPads.find(p => p.id === activeHex);
    sendHexMessage(pad.note, 0.0);
  }
}

function updateTrillState() {
  activeTrill = [];
  for (let bar of trillBars) {
    for (let j = 0; j < bar.zones.length; j++) {
      if (bar.zones[j].active) {
        activeTrill.push(`/euphoria/trill/${bar.id}/${j}`);
      }
    }
  }
}

// Send OSC-style messages to Bela
function sendHexMessage(note, value = 1.0) {
  let message = `/euphoria/hex/${note} ${value.toFixed(2)}`;
  console.log(message);
  
  // In actual Bela implementation, you would send this via WebSocket:
  // if (typeof Bela !== 'undefined' && Bela.control) {
  //   Bela.control.send(message);
  // }
}

function sendTrillMessage(barId, zoneId, position) {
  let message = `/euphoria/trill/${barId}/${zoneId} ${position.toFixed(3)}`;
  console.log(message);
  
  // In actual Bela implementation:
  // if (typeof Bela !== 'undefined' && Bela.control) {
  //   Bela.control.send(message);
  // }
}

// Keyboard shortcuts for testing
function keyPressed() {
  // Number keys 1-7 for hex pads
  if (key >= '1' && key <= '7') {
    let note = parseInt(key);
    activeHex = note;
    sendHexMessage(note);
  }
  
  // Q,W,E,R for trill bars
  let trillKeys = ['q', 'w', 'e', 'r'];
  let keyIndex = trillKeys.indexOf(key.toLowerCase());
  if (keyIndex !== -1) {
    let bar = trillBars[keyIndex];
    bar.zones[0].active = !bar.zones[0].active;
    updateTrillState();
    sendTrillMessage(bar.id, 0, bar.zones[0].active ? 0.5 : 0.0);
  }
  
  // A,S,D,F for bottom zones
  let trillKeysBottom = ['a', 's', 'd', 'f'];
  let keyIndexBottom = trillKeysBottom.indexOf(key.toLowerCase());
  if (keyIndexBottom !== -1) {
    let bar = trillBars[keyIndexBottom];
    bar.zones[1].active = !bar.zones[1].active;
    updateTrillState();
    sendTrillMessage(bar.id, 1, bar.zones[1].active ? 0.5 : 0.0);
  }
}