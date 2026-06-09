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
    nextLayerId: 1,
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

  function createLayerId() {
    var id = "layer-" + state.nextLayerId;
    state.nextLayerId += 1;
    return id;
  }

  function addLayer(layer) {
    var id = createLayerId();
    var normalized = Object.assign(
      {
        id: id,
        name: "图层 " + id.replace("layer-", ""),
        visible: true,
        locked: false,
        x: 0,
        y: 0,
        width: 1,
        height: 1,
      },
      layer
    );
    state.project.layers.push(normalized);
    selectLayer(normalized.id);
    renderProject();
    renderLayerList();
    updateLayerControls();
    return normalized;
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

  function validateCanvasSize(width, height) {
    var parsedWidth = Number.parseInt(width, 10);
    var parsedHeight = Number.parseInt(height, 10);

    if (!Number.isInteger(parsedWidth) || !Number.isInteger(parsedHeight)) {
      return { ok: false, message: "画布宽高必须是整数" };
    }
    if (parsedWidth < 1 || parsedHeight < 1) {
      return { ok: false, message: "画布宽高必须大于 0" };
    }
    if (parsedWidth > MAX_CANVAS_SIZE || parsedHeight > MAX_CANVAS_SIZE) {
      return { ok: false, message: "画布宽高不能超过 " + MAX_CANVAS_SIZE };
    }

    return {
      ok: true,
      width: parsedWidth,
      height: parsedHeight,
    };
  }

  function setScreenSize(width, height, presetId) {
    var preset = SCREEN_PRESETS.find(function (item) {
      return item.id === presetId;
    });
    var result = validateCanvasSize(width, height);
    if (!result.ok) {
      showMessage(result.message, "error");
      updateScreenControls();
      return false;
    }

    state.project.canvas.width = result.width;
    state.project.canvas.height = result.height;
    state.project.screenPreset = preset ? preset.label : "custom";
    resizeCanvases(result.width, result.height);
    updateScreenControls();
    fitCanvasToViewport();
    renderProject();
    showMessage("画布尺寸已更新");
    return true;
  }

  function applyScreenSizeFromControls() {
    setScreenSize(refs.canvasWidth.value, refs.canvasHeight.value, refs.screenPreset.value);
  }

  function getSelectedLayer() {
    if (!state.selectedId) {
      return null;
    }
    return state.project.layers.find(function (layer) {
      return layer.id === state.selectedId;
    }) || null;
  }

  function selectLayer(id) {
    var exists = state.project.layers.some(function (layer) {
      return layer.id === id;
    });
    state.selectedId = exists ? id : null;
    renderLayerList();
    updateLayerControls();
    renderOverlay();
  }

  function layerTypeLabel(type) {
    var labels = {
      image: "图片",
      rect: "矩形",
      ellipse: "圆",
      line: "直线",
      polygon: "多边形",
    };
    return labels[type] || type;
  }

  function renderLayerList() {
    refs.layerList.textContent = "";

    if (!state.project.layers.length) {
      var empty = document.createElement("p");
      empty.className = "empty-state";
      empty.textContent = "暂无图层";
      refs.layerList.appendChild(empty);
      return;
    }

    state.project.layers
      .slice()
      .reverse()
      .forEach(function (layer) {
        var row = document.createElement("div");
        row.className = "layer-row";
        row.classList.toggle("active", layer.id === state.selectedId);
        row.classList.toggle("hidden-layer", layer.visible === false);
        row.classList.toggle("locked-layer", layer.locked === true);
        row.dataset.layerId = layer.id;

        var title = document.createElement("div");
        title.className = "layer-title";

        var name = document.createElement("strong");
        name.textContent = layer.name || layerTypeLabel(layer.type);
        title.appendChild(name);

        var type = document.createElement("span");
        type.textContent = layerTypeLabel(layer.type);
        title.appendChild(type);

        var visible = document.createElement("button");
        visible.type = "button";
        visible.title = layer.visible === false ? "显示图层" : "隐藏图层";
        visible.textContent = layer.visible === false ? "隐" : "显";
        visible.addEventListener("click", function (event) {
          event.stopPropagation();
          layer.visible = layer.visible === false;
          renderProject();
          renderLayerList();
        });

        var locked = document.createElement("button");
        locked.type = "button";
        locked.title = layer.locked ? "解锁图层" : "锁定图层";
        locked.textContent = layer.locked ? "锁" : "开";
        locked.addEventListener("click", function (event) {
          event.stopPropagation();
          layer.locked = !layer.locked;
          renderLayerList();
          updateLayerControls();
          renderOverlay();
        });

        row.appendChild(title);
        row.appendChild(visible);
        row.appendChild(locked);
        row.addEventListener("click", function () {
          selectLayer(layer.id);
        });
        refs.layerList.appendChild(row);
      });
  }

  function setInputValue(input, value) {
    input.value = value === undefined || value === null ? "" : String(value);
  }

  function setFieldEnabled(input, enabled) {
    input.disabled = !enabled;
  }

  function updateLayerControls() {
    var layer = getSelectedLayer();
    var hasLayer = Boolean(layer);
    refs.noSelection.hidden = hasLayer;
    refs.selectedPanel.hidden = !hasLayer;

    if (!layer) {
      return;
    }

    setInputValue(refs.layerName, layer.name);
    setInputValue(refs.propX, Math.round(layer.x || 0));
    setInputValue(refs.propY, Math.round(layer.y || 0));
    setInputValue(refs.propWidth, Math.round(layer.width || 1));
    setInputValue(refs.propHeight, Math.round(layer.height || 1));
    setInputValue(refs.fillGray, layer.fillGray);
    setInputValue(refs.strokeGray, layer.strokeGray);
    setInputValue(refs.strokeWidth, layer.strokeWidth);
    setInputValue(refs.opacity, typeof layer.opacity === "number" ? layer.opacity : 1);

    var usesFill = layer.type === "rect" || layer.type === "ellipse" || layer.type === "polygon";
    var usesStroke = layer.type !== "image";
    var usesOpacity = layer.type === "image";

    setFieldEnabled(refs.fillGray, usesFill);
    setFieldEnabled(refs.strokeGray, usesStroke);
    setFieldEnabled(refs.strokeWidth, usesStroke);
    setFieldEnabled(refs.opacity, usesOpacity);
  }

  function updateSelectedLayer(mutator) {
    var layer = getSelectedLayer();
    if (!layer || layer.locked) {
      return;
    }
    mutator(layer);
    renderProject();
    renderLayerList();
    updateLayerControls();
  }

  function bindPropertyInput(input, handler) {
    input.addEventListener("input", function () {
      updateSelectedLayer(function (layer) {
        handler(layer, input.value);
      });
    });
  }

  function moveSelectedLayer(delta) {
    var layer = getSelectedLayer();
    if (!layer) {
      return;
    }
    var layers = state.project.layers;
    var index = layers.indexOf(layer);
    var nextIndex = clamp(index + delta, 0, layers.length - 1);
    if (index === nextIndex) {
      return;
    }
    layers.splice(index, 1);
    layers.splice(nextIndex, 0, layer);
    renderProject();
    renderLayerList();
  }

  function deleteSelectedLayer() {
    var layer = getSelectedLayer();
    if (!layer) {
      return;
    }
    state.project.layers = state.project.layers.filter(function (item) {
      return item.id !== layer.id;
    });
    state.selectedId = null;
    renderProject();
    renderLayerList();
    updateLayerControls();
  }

  function bindLayerEvents() {
    refs.applyScreenSize.addEventListener("click", applyScreenSizeFromControls);
    refs.screenPreset.addEventListener("change", function () {
      var preset = SCREEN_PRESETS.find(function (item) {
        return item.id === refs.screenPreset.value;
      });
      if (preset) {
        refs.canvasWidth.value = String(preset.width);
        refs.canvasHeight.value = String(preset.height);
        setScreenSize(preset.width, preset.height, preset.id);
      }
    });

    bindPropertyInput(refs.layerName, function (layer, value) {
      layer.name = value.trim() || layerTypeLabel(layer.type);
    });
    bindPropertyInput(refs.propX, function (layer, value) {
      layer.x = clampInt(value, -MAX_CANVAS_SIZE, MAX_CANVAS_SIZE);
    });
    bindPropertyInput(refs.propY, function (layer, value) {
      layer.y = clampInt(value, -MAX_CANVAS_SIZE, MAX_CANVAS_SIZE);
    });
    bindPropertyInput(refs.propWidth, function (layer, value) {
      layer.width = clampInt(value, 1, MAX_CANVAS_SIZE);
    });
    bindPropertyInput(refs.propHeight, function (layer, value) {
      layer.height = clampInt(value, 1, MAX_CANVAS_SIZE);
    });
    bindPropertyInput(refs.fillGray, function (layer, value) {
      layer.fillGray = clampInt(value, 0, 15);
    });
    bindPropertyInput(refs.strokeGray, function (layer, value) {
      layer.strokeGray = clampInt(value, 0, 15);
    });
    bindPropertyInput(refs.strokeWidth, function (layer, value) {
      layer.strokeWidth = clampInt(value, 0, 512);
    });
    bindPropertyInput(refs.opacity, function (layer, value) {
      var parsed = Number.parseFloat(value);
      layer.opacity = Number.isFinite(parsed) ? clamp(parsed, 0, 1) : 1;
    });

    refs.moveLayerUp.addEventListener("click", function () {
      moveSelectedLayer(1);
    });
    refs.moveLayerDown.addEventListener("click", function () {
      moveSelectedLayer(-1);
    });
    refs.deleteLayer.addEventListener("click", deleteSelectedLayer);
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
    bindLayerEvents();
    resizeCanvases(state.project.canvas.width, state.project.canvas.height);
    updateScreenControls();
    renderLayerList();
    updateLayerControls();
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
    addLayer: addLayer,
    selectLayer: selectLayer,
    setScreenSize: setScreenSize,
  };

  document.addEventListener("DOMContentLoaded", init);
})();
