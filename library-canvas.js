mergeInto(LibraryManager.library, {
	canvas_create_context: function() {
		Module.ctx = Module.canvas.getContext("2d");
	        Module.imageData = Module.ctx.createImageData(Module.canvas.width, Module.canvas.height);
	},
	canvas_begin_time: function(x1, y1, x2, y2) {
		Module.canvas_startTime = window.performance.now();
	},
	canvas_end_time: function(x1, y1, x2, y2) {
		window.console.log(window.performance.now() - Module.canvas_startTime);
	},
	canvas_set_fill: function (r, g, b, a) {
		Module.ctx.fillStyle = "rgba(" + Math.floor(r*255) + ", " + Math.floor(g*255) + ", " + Math.floor(b*255) + ", " + a + ")";
		Module.ctx.beginPath();
	},
	canvas_clear: function (r, g, b, a) {
		Module.ctx.clearRect(0, 0, Module.canvas.width, Module.canvas.height);
	},

	canvas_move_to: function(x, y) {
		Module.ctx.moveTo(x,y);
	},
	canvas_line_to: function(x, y) {
		Module.ctx.lineTo(x,y);
	},
	canvas_quad_to: function(x1, y1, x2, y2) {
		Module.ctx.quadraticCurveTo(x1, y1, x2, y2);
	},
	canvas_fill: function() {
		Module.ctx.fill('evenodd');
	},
	
	canvas_put_image: function(ptr, width, height) {
	        var imageData = Module.imageData;
		var data = imageData.data;
		var size = height * width*4;
		for (var i=0; i<size; i+=4) {
			data[i] = HEAPU8[(ptr + i+2)|0];
			data[i+1] = HEAPU8[(ptr + i+1)|0];
			data[i+2] = HEAPU8[(ptr + i)|0];
			data[i+3] = HEAPU8[(ptr + i+3)|0];
			HEAPU8[(ptr + i+2)|0] = 0;
			HEAPU8[(ptr + i+1)|0] = 0;
			HEAPU8[(ptr + i)|0] = 0;
			HEAPU8[(ptr + i+3)|0] = 0;
		}
		Module.ctx.putImageData(imageData, 0, 0);
	        //Module.imageData = Module.ctx.createImageData(Module.canvas.width, Module.canvas.height);
	},

	webgl_init: function() {
		var vertexShaderString = 
		'attribute vec2 vertexPosition;                              \n\
			varying vec2 texCoord;                                      \n\
			void main(void) {                                           \n\
				texCoord = vec2(vertexPosition.x, 1.0 - vertexPosition.y);                                \n\
			gl_Position = vec4(2.0 * vertexPosition - 1.0, 0.0, 1.0); \n\
			}                                                           \n';

		var fragmentShaderString =
		'precision mediump float;                          \n\
			uniform sampler2D texSampler;                     \n\
			varying vec2 texCoord;                            \n\
			void main(void) {                                 \n\
			        /* the rasterizer writes out data in bgra format */ \n\
				gl_FragColor = texture2D(texSampler, texCoord).bgra;  \n\
			}                                                 \n';

		var gl, texarray, program;
		var texUnit = 0; // we will use texture unit 0 only

		gl = Module.canvas.getContext("experimental-webgl");

		var vertexShader = gl.createShader(gl.VERTEX_SHADER);
		gl.shaderSource(vertexShader, vertexShaderString);
		gl.compileShader(vertexShader);

		var fragmentShader = gl.createShader(gl.FRAGMENT_SHADER);
		gl.shaderSource(fragmentShader, fragmentShaderString);
		gl.compileShader(fragmentShader);

		program = gl.createProgram();
		gl.attachShader(program, vertexShader);
		gl.attachShader(program, fragmentShader);
		gl.linkProgram(program);
		gl.useProgram(program);

		var vertexPositionAttrLoc = gl.getAttribLocation(program, "vertexPosition");
		gl.enableVertexAttribArray(vertexPositionAttrLoc);
		var texSamplerLoc = gl.getUniformLocation(program, "texSampler");
		gl.uniform1i(texSamplerLoc, texUnit);

		var vertexPositionBuffer = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, vertexPositionBuffer);
		var vertices = [ 0.0,  0.0,
			0.0,  1.0,
			1.0,  0.0,
			1.0,  1.0 ];
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.STATIC_DRAW);
		gl.vertexAttribPointer(vertexPositionAttrLoc, 2, gl.FLOAT, false, 0, 0);

		texarray = new Uint8Array([0, 0, 0, 0,       0, 0, 0, 0,       255, 0, 0, 255,   0, 0, 0, 0,       0, 0, 0, 0,
			0, 0, 0, 0,       255, 128, 0, 255,   0, 0, 0, 0,       255, 128, 0, 255,   0, 0, 0, 0,
			255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 0, 255,
			0, 255, 0, 255,   0, 0, 0, 0,       0, 0, 0, 0,       0, 0, 0, 0,       0, 255, 0, 255,
		0, 0, 255, 255,   0, 0, 0, 0,       0, 0, 0, 0,       0, 0, 0, 0,       0, 0, 255, 255]);

		texture = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, texture);
		//gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true); // so we can have the above array upright. Otherwise, it helps performance NOT to flip.
		gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4); // 4 is the default. added for explicitness. common pitfall.
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); // so it works with this non-power-of-two texture
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.bindTexture(gl.TEXTURE_2D, null);
		Module.ctx = gl;
	},


	webgl_put_image: function(ptr, width, height) {
		var pixels = {{{ makeHEAPView('U8', 'ptr', 'ptr+width*height*4') }}}
		var gl = Module.ctx;
		gl.activeTexture(gl.TEXTURE0); // so we're being explicit with texture units. But here, texUnit is set to 0 so this is just pedantic.
		gl.bindTexture(gl.TEXTURE_2D, texture);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, pixels);

		gl.clearColor(0.0, 0.0, 0.0, 1.0);
		gl.clear(gl.COLOR_BUFFER_BIT);
		gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);


	//gl.bindTexture(gl.TEXTURE_2D, texture);
	//gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
	//gl.drawArrays(gl.TRIANGLES, 0, 6);
	//putImageData(imageData, 0, 0);
	//Module.imageData = Module.ctx.createImageData(Module.canvas.width, Module.canvas.height);
	}
});
