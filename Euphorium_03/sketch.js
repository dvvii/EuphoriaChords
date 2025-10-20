function setup() {
	// Create a canvas
	createCanvas(windowWidth, windowHeight);
	let buffer = [0, 0]

	// Create key selector
	keySelector = createSelect();
	keySelector.option('C', 0);
	keySelector.option('C#/Db', 1);
	keySelector.option('D', 2);
	keySelector.option('D#/Eb', 3);
	keySelector.option('E', 4);
	keySelector.option('F', 5);
	keySelector.option('F#/Gb', 6);
	keySelector.option('G', 7);
	keySelector.option('G#/Ab', 8);
	keySelector.option('A', 9);
	keySelector.option('A#/Bb', 10);
	keySelector.option('B', 11);
	keySelector.selected('C');

	// Create sliders
	toneSlider = createSlider(0, 100, 50);
	decaySlider = createSlider(0, 2000, 500);
	modSlider = createSlider(0, 32, 5);
	adsrSlider = createSlider(0, 50, 10);
	vibFreqSlider = createSlider(0, 127, 0);
	vibDepthSlider = createSlider(0, 127, 0);

	// Create text elements
	p0 = createP("Key");
	p1 = createP("Tone");
	p2 = createP("Decay");
	p3 = createP("Modulation");
	p4 = createP("ADSR");
	p5 = createP("Vibrato Frequency");
	p6 = createP("Vibrato Depth");

	// Format DOM elements with styling
	formatDOMElements();
}

function draw() {
	// Background gradient effect
	background(10, 10, 46);

	// Get key selector value
	_keyValue = int(keySelector.value());

	// Get slider values
	_toneSlider = toneSlider.value() / 100;
	_decaySlider = decaySlider.value();
	_modSlider = modSlider.value();
	_adsrSlider = adsrSlider.value();
	_vibFreqSlider = vibFreqSlider.value();
	_vibDepthSlider = vibDepthSlider.value();

	// Send to Bela (if connected)
	if (typeof Bela !== 'undefined') {
		Bela.control.send({key: _keyValue});
		Bela.control.send({tone: _toneSlider});
		Bela.control.send({decay: _decaySlider});
		Bela.control.send({mod: _modSlider});
		Bela.control.send({adsr: _adsrSlider});
		Bela.control.send({vibFreq: _vibFreqSlider});
		Bela.control.send({vibDepth: _vibDepthSlider});
	}
}

function formatDOMElements() {
	// Calculate center position and starting Y position
	let centerX = windowWidth / 2;
	let startY = 80;
	let spacing = 90;
	let sliderWidth = min(300, windowWidth - 80);

	// Style and position key selector
	styleSelector(keySelector, centerX, startY, sliderWidth);

	// Style and position sliders (with offset for key selector)
	styleSlider(toneSlider, centerX, startY + spacing, sliderWidth);
	styleSlider(decaySlider, centerX, startY + spacing * 2, sliderWidth);
	styleSlider(modSlider, centerX, startY + spacing * 3, sliderWidth);
	styleSlider(adsrSlider, centerX, startY + spacing * 4, sliderWidth);
	styleSlider(vibFreqSlider, centerX, startY + spacing * 5, sliderWidth);
	styleSlider(vibDepthSlider, centerX, startY + spacing * 6, sliderWidth);

	// Style and position text labels
	styleLabel(p0, centerX, startY - 35, sliderWidth);
	styleLabel(p1, centerX, startY + spacing - 35, sliderWidth);
	styleLabel(p2, centerX, startY + spacing * 2 - 35, sliderWidth);
	styleLabel(p3, centerX, startY + spacing * 3 - 35, sliderWidth);
	styleLabel(p4, centerX, startY + spacing * 4 - 35, sliderWidth);
	styleLabel(p5, centerX, startY + spacing * 5 - 35, sliderWidth);
	styleLabel(p6, centerX, startY + spacing * 6 - 35, sliderWidth);
}

function styleSlider(slider, x, y, width) {
	slider.position(x - width / 2, y);
	slider.style('width', width + 'px');
	slider.style('height', '8px');
	slider.style('background', 'linear-gradient(90deg, #667eea 0%, #764ba2 100%)');
	slider.style('border-radius', '10px');
	slider.style('outline', 'none');
	slider.style('opacity', '0.9');
	slider.style('-webkit-appearance', 'none');
	slider.style('appearance', 'none');

	// Add container styling
	slider.style('padding', '16px 20px');
	slider.style('background-color', 'rgba(255, 255, 255, 0.05)');
	slider.style('border', '1px solid rgba(102, 126, 234, 0.2)');
	slider.style('border-radius', '16px');
	slider.style('box-shadow', '0 4px 20px rgba(0, 0, 0, 0.3)');
}

function styleSelector(selector, x, y, width) {
	selector.position(x - width / 2, y);
	selector.style('width', width + 'px');
	selector.style('height', '48px');
	selector.style('background-color', 'rgba(255, 255, 255, 0.05)');
	selector.style('color', '#e0e0ff');
	selector.style('border', '1px solid rgba(102, 126, 234, 0.2)');
	selector.style('border-radius', '16px');
	selector.style('padding', '12px 20px');
	selector.style('font-family', '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif');
	selector.style('font-size', '16px');
	selector.style('font-weight', '600');
	selector.style('outline', 'none');
	selector.style('box-shadow', '0 4px 20px rgba(0, 0, 0, 0.3)');
	selector.style('-webkit-appearance', 'none');
	selector.style('appearance', 'none');
}

function styleLabel(label, x, y, width) {
	label.position(x - width / 2, y);
	label.style('width', width + 'px');
	label.style('color', '#e0e0ff');
	label.style('font-family', '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif');
	label.style('font-size', '14px');
	label.style('font-weight', '600');
	label.style('letter-spacing', '1px');
	label.style('text-transform', 'uppercase');
	label.style('margin', '0');
	label.style('padding', '0');
	label.style('text-align', 'left');
}

function windowResized() {
	resizeCanvas(windowWidth, windowHeight);
	formatDOMElements();
}