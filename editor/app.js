(function () {
  "use strict";

  var SCREEN_PRESETS = [
    {
      id: "ed060kd1-portrait",
      label: "ED060KD1 portrait",
      width: 1072,
      height: 1448,
    },
  ];

  var MAX_CANVAS_SIZE = 4096;
  var MIN_ZOOM = 0.1;
  var MAX_ZOOM = 4;

  var state = {
    project: null,
    selectedId: null,
    activeTool: "select",
    zoom: 1,
    drag: null,
    polygonDraft: null,
    messageTimer: null,
  };

  var refs = {};
  var workCanvas = document.createElement("canvas");
  var workCtx = workCanvas.getContext("2d", { willReadFrequently: true });

  var REF_IDS = [
    "screenPreset",
    "canvasWidth",
    "canvasHeight",
    "applyScreenSize",
    "imageInput",
    "projectInput",
    "saveProject",
    "importProject",
    "exportBin",
    "exportRaw",
    "exportBmp",
    "toolSelect",
    "toolRect",
    "toolEllipse",
    "toolLine",
    "toolPolygon",
    "finishPolygon",
    "zoomOut",
    "zoomLevel",
    "zoomIn",
    "fitCanvas",
    "displayCanvas",
    "overlayCanvas",
    "canvasStatus",
    "selectedPanel",
    "noSelection",
    "layerName",
    "propX",
    "propY",
    "propWidth",
    "propHeight",
    "fillGray",
    "strokeGray",
    "strokeWidth",
    "opacity",
    "deleteLayer",
    "moveLayerUp",
    "moveLayerDown",
    "layerList",
    "message",
  ];

  function bindRefs() {
    REF_IDS.forEach(function (id) {
      refs[id] = document.getElementById(id);
      if (!refs[id]) {
        throw new Error("Missing DOM node: " + id);
      }
    });
    refs.canvasStack = document.getElementById("canvasStack");
    refs.canvasViewport = document.getElementById("canvasViewport");
    refs.displayCtx = refs.displayCanvas.getContext("2d", { willReadFrequently: true });
    refs.overlayCtx = refs.overlayCanvas.getContext("2d");
  }

  function createDefaultProject() {
    return {
      version: 1,
      screenPreset: "ED060KD1 portrait",
      canvas: {
        width: 1072,
        height: 1448,
      },
      layers: [],
    };
  }

  function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
  }

  function clampInt(value, min, max) {
    var parsed = Number.parseInt(value, 10);
    if (!Number.isFinite(parsed)) {
      return min;
    }
    return clamp(parsed, min, max);
  }

  function showMessage(text, kind) {
    refs.message.textContent = text || "";
    refs.message.classList.toggle("error", kind === "error");
    if (state.messageTimer) {
      window.clearTimeout(state.messageTimer);
    }
    if (text) {
      state.messageTimer = window.setTimeout(function () {
        refs.message.textContent = "";
        refs.message.classList.remove("error");
      }, kind === "error" ? 5000 : 2500);
    }
  }

  function resizeCanvases(width, height) {
    refs.displayCanvas.width = width;
    refs.displayCanvas.height = height;
    refs.overlayCanvas.width = width;
    refs.overlayCanvas.height = height;
    workCanvas.width = width;
    workCanvas.height = height;
    updateCanvasCssSize();
  }

  function updateCanvasCssSize() {
    var width = state.project.canvas.width;
    var height = state.project.canvas.height;
    var cssWidth = Math.max(1, Math.round(width * state.zoom));
    var cssHeight = Math.max(1, Math.round(height * state.zoom));

    refs.canvasStack.style.width = cssWidth + "px";
    refs.canvasStack.style.height = cssHeight + "px";
    refs.displayCanvas.style.width = cssWidth + "px";
    refs.displayCanvas.style.height = cssHeight + "px";
    refs.overlayCanvas.style.width = cssWidth + "px";
    refs.overlayCanvas.style.height = cssHeight + "px";
    refs.zoomLevel.textContent = Math.round(state.zoom * 100) + "%";
  }

  function setZoom(nextZoom) {
    state.zoom = clamp(nextZoom, MIN_ZOOM, MAX_ZOOM);
    updateCanvasCssSize();
    renderOverlay();
  }

  function fitCanvasToViewport() {
    var viewport = refs.canvasViewport.getBoundingClientRect();
    var width = state.project.canvas.width;
    var height = state.project.canvas.height;
    var availableWidth = Math.max(120, viewport.width - 56);
    var availableHeight = Math.max(120, viewport.height - 56);
    setZoom(Math.min(availableWidth / width, availableHeight / height, 1));
  }

  function grayToByte(gray) {
    return clamp(Math.round(gray), 0, 15) * 17;
  }

  function grayToCss(gray) {
    var byte = grayToByte(gray);
    return "rgb(" + byte + " " + byte + " " + byte + ")";
  }

  function renderProject() {
    var width = state.project.canvas.width;
    var height = state.project.canvas.height;
    var grayPixels = renderToGrayPixels();
    paintGray16ToDisplay(grayPixels.pixels, width, height);
    renderOverlay();
    updateCanvasStatus();
  }

  function renderToGrayPixels() {
    var width = state.project.canvas.width;
    var height = state.project.canvas.height;
    if (workCanvas.width !== width || workCanvas.height !== height) {
      workCanvas.width = width;
      workCanvas.height = height;
    }

    workCtx.save();
    workCtx.setTransform(1, 0, 0, 1, 0, 0);
    workCtx.clearRect(0, 0, width, height);
    workCtx.fillStyle = "#fff";
    workCtx.fillRect(0, 0, width, height);
    state.project.layers.forEach(function (layer) {
      if (layer.visible !== false) {
        drawLayer(workCtx, layer);
      }
    });
    workCtx.restore();

    return {
      width: width,
      height: height,
      pixels: quantizeCanvasToGray16(workCanvas),
    };
  }

  function drawLayer(ctx, layer) {
    if (!layer || layer.visible === false) {
      return;
    }

    ctx.save();
    ctx.globalAlpha = typeof layer.opacity === "number" ? clamp(layer.opacity, 0, 1) : 1;

    if (layer.type === "rect") {
      ctx.fillStyle = grayToCss(layer.fillGray);
      ctx.strokeStyle = grayToCss(layer.strokeGray);
      ctx.lineWidth = Math.max(0, layer.strokeWidth || 0);
      ctx.fillRect(layer.x, layer.y, layer.width, layer.height);
      if (ctx.lineWidth > 0) {
        ctx.strokeRect(layer.x, layer.y, layer.width, layer.height);
      }
    }

    ctx.restore();
  }

  function quantizeCanvasToGray16(sourceCanvas) {
    var width = sourceCanvas.width;
    var height = sourceCanvas.height;
    var sourceCtx = sourceCanvas.getContext("2d", { willReadFrequently: true });
    var image = sourceCtx.getImageData(0, 0, width, height);
    var data = image.data;
    var pixels = new Uint8Array(width * height);

    for (var i = 0, p = 0; i < data.length; i += 4, p += 1) {
      var alpha = data[i + 3] / 255;
      var r = data[i] * alpha + 255 * (1 - alpha);
      var g = data[i + 1] * alpha + 255 * (1 - alpha);
      var b = data[i + 2] * alpha + 255 * (1 - alpha);
      var luminance = 0.299 * r + 0.587 * g + 0.114 * b;
      pixels[p] = clamp(Math.round(luminance / 17), 0, 15);
    }

    return pixels;
  }

  function paintGray16ToDisplay(grayPixels, width, height) {
    var image = refs.displayCtx.createImageData(width, height);
    var data = image.data;

    for (var i = 0, p = 0; p < grayPixels.length; i += 4, p += 1) {
      var byte = grayToByte(grayPixels[p]);
      data[i] = byte;
      data[i + 1] = byte;
      data[i + 2] = byte;
      data[i + 3] = 255;
    }

    refs.displayCtx.putImageData(image, 0, 0);
  }

  function renderOverlay() {
    refs.overlayCtx.clearRect(0, 0, refs.overlayCanvas.width, refs.overlayCanvas.height);
  }

  function updateCanvasStatus() {
    refs.canvasStatus.textContent =
      state.project.canvas.width + " x " + state.project.canvas.height + " · 16 灰阶";
  }

  function updateScreenControls() {
    refs.canvasWidth.value = String(state.project.canvas.width);
    refs.canvasHeight.value = String(state.project.canvas.height);
    var preset = SCREEN_PRESETS.find(function (item) {
      return item.label === state.project.screenPreset;
    });
    refs.screenPreset.value = preset ? preset.id : "custom";
  }

  function bindCoreEvents() {
    refs.zoomOut.addEventListener("click", function () {
      setZoom(state.zoom / 1.2);
    });
    refs.zoomIn.addEventListener("click", function () {
      setZoom(state.zoom * 1.2);
    });
    refs.fitCanvas.addEventListener("click", fitCanvasToViewport);
    window.addEventListener("resize", function () {
      fitCanvasToViewport();
    });
  }

  function init() {
    bindRefs();
    state.project = createDefaultProject();
    bindCoreEvents();
    resizeCanvases(state.project.canvas.width, state.project.canvas.height);
    updateScreenControls();
    fitCanvasToViewport();
    renderProject();
  }

  window.epdEditor = {
    state: state,
    renderProject: renderProject,
    renderToGrayPixels: renderToGrayPixels,
    quantizeCanvasToGray16: quantizeCanvasToGray16,
    grayToByte: grayToByte,
    grayToCss: grayToCss,
  };

  document.addEventListener("DOMContentLoaded", init);
})();
