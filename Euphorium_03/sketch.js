function setup() {
	//Create a thin canvas where to allocate the elements (this is not strictly neccessary because
	//we will use DOM elements which can be allocated directly in the window)
	createCanvas(windowWidth /5, windowHeight * 2 / 3);
	let buffer = [0,0]

	tSlider = createSlider(0, 100, 60);
	dSlider = createSlider(0, 100, 50);

	cButton= createButton("C");
//	cButton.mouseClicked(changeButtonState)

	//Text
	p1 = createP("Tone:");
	p2 = createP("Decay:");

	//This function will format colors and positions of the DOM elements (sliders, button and text)
	formatDOMElements();

}

function draw() {

    background(5,5,255,1);
	_tSlider = tSlider.value()/100;
    _dSlider = dSlider.value()*2000;
	
	Bela.control.send({tone: _tSlider});
	Bela.control.send({decay: _dSlider});

//
	
//store values in the buffer
//	buffer[0]=tSlider.value()/100;
//	buffer[1]=dSlider.value()/100;

	//SEND BUFFER to Bela. Buffer has index 0 (to be read by Bela),
	//contains floats and sends the 'buffer' array.
  //  Bela.data.sendBuffer(0, 'float', [100, 102]);
}

function formatDOMElements() {

	//Format sliders
	tSlider.position((windowWidth-tSlider.width)/2,windowHeight/2 + 20);
	dSlider.position((windowWidth-dSlider.width)/2,windowHeight/2 + 90);


	//Format text as paragraphs
	p1.position((windowWidth-tSlider.width)/2,windowHeight/2-20);
	p2.position((windowWidth-dSlider.width)/2,windowHeight/2+50);
}
