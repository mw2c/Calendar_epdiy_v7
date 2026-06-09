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
    imageCache: new Map(),
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

  function getMaxLayerNumber() {
    return state.project.layers.reduce(function (max, layer) {
      var match = /^layer-(\d+)$/.exec(layer.id || "");
      return match ? Math.max(max, Number.parseInt(match[1], 10)) : max;
    }, 0);
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

  function readFileAsDataURL(file) {
    return new Promise(function (resolve, reject) {
      var reader = new FileReader();
      reader.onload = function () {
        resolve(String(reader.result || ""));
      };
      reader.onerror = function () {
        reject(new Error("文件读取失败"));
      };
      reader.readAsDataURL(file);
    });
  }

  function loadImage(src) {
    return new Promise(function (resolve, reject) {
      var image = new Image();
      image.onload = function () {
        resolve(image);
      };
      image.onerror = function () {
        reject(new Error("图片解码失败"));
      };
      image.src = src;
    });
  }

  function createImageLayer(src, image, fileName) {
    var maxWidth = state.project.canvas.width * 0.8;
    var maxHeight = state.project.canvas.height * 0.8;
    var ratio = Math.min(maxWidth / image.naturalWidth, maxHeight / image.naturalHeight, 1);
    var width = Math.max(1, Math.round(image.naturalWidth * ratio));
    var height = Math.max(1, Math.round(image.naturalHeight * ratio));

    var layer = addLayer({
      type: "image",
      name: fileName || "导入图片",
      src: src,
      sourceWidth: image.naturalWidth,
      sourceHeight: image.naturalHeight,
      x: Math.round((state.project.canvas.width - width) / 2),
      y: Math.round((state.project.canvas.height - height) / 2),
      width: width,
      height: height,
      opacity: 1,
    });
    state.imageCache.set(layer.id, image);
    renderProject();
    return layer;
  }

  function handleImageImport(file) {
    if (!file) {
      return;
    }

    var accepted = ["image/svg+xml", "image/png", "image/jpeg"];
    if (accepted.indexOf(file.type) === -1) {
      showMessage("仅支持 SVG、PNG、JPG 图片", "error");
      refs.imageInput.value = "";
      return;
    }

    return readFileAsDataURL(file)
      .then(function (src) {
        return loadImage(src).then(function (image) {
          createImageLayer(src, image, file.name);
          showMessage("图片已导入");
        });
      })
      .catch(function (error) {
        showMessage(error.message, "error");
      })
      .finally(function () {
        refs.imageInput.value = "";
      });
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

    if (layer.type === "ellipse") {
      ctx.fillStyle = grayToCss(layer.fillGray);
      ctx.strokeStyle = grayToCss(layer.strokeGray);
      ctx.lineWidth = Math.max(0, layer.strokeWidth || 0);
      ctx.beginPath();
      ctx.ellipse(
        layer.x + layer.width / 2,
        layer.y + layer.height / 2,
        Math.abs(layer.width / 2),
        Math.abs(layer.height / 2),
        0,
        0,
        Math.PI * 2
      );
      ctx.fill();
      if (ctx.lineWidth > 0) {
        ctx.stroke();
      }
    }

    if (layer.type === "line") {
      var linePoints = absoluteLayerPoints(layer);
      if (linePoints.length >= 2) {
        ctx.strokeStyle = grayToCss(layer.strokeGray);
        ctx.lineWidth = Math.max(1, layer.strokeWidth || 1);
        ctx.lineCap = "round";
        ctx.beginPath();
        ctx.moveTo(linePoints[0].x, linePoints[0].y);
        ctx.lineTo(linePoints[1].x, linePoints[1].y);
        ctx.stroke();
      }
    }

    if (layer.type === "polygon") {
      var polygonPoints = absoluteLayerPoints(layer);
      if (polygonPoints.length >= 3) {
        ctx.fillStyle = grayToCss(layer.fillGray);
        ctx.strokeStyle = grayToCss(layer.strokeGray);
        ctx.lineWidth = Math.max(0, layer.strokeWidth || 0);
        ctx.beginPath();
        ctx.moveTo(polygonPoints[0].x, polygonPoints[0].y);
        polygonPoints.slice(1).forEach(function (point) {
          ctx.lineTo(point.x, point.y);
        });
        ctx.closePath();
        ctx.fill();
        if (ctx.lineWidth > 0) {
          ctx.stroke();
        }
      }
    }

    if (layer.type === "image") {
      var image = state.imageCache.get(layer.id);
      if (image) {
        ctx.drawImage(image, layer.x, layer.y, layer.width, layer.height);
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
    drawDraftOverlay(refs.overlayCtx);
    var layer = getSelectedLayer();
    if (layer) {
      drawSelectionBox(refs.overlayCtx, layer);
    }
  }

  function drawDraftOverlay(ctx) {
    if (state.drag && state.drag.mode === "draw" && state.drag.draftLayer) {
      ctx.save();
      ctx.globalAlpha = 0.8;
      drawLayer(ctx, state.drag.draftLayer);
      ctx.restore();
    }

    if (state.polygonDraft && state.polygonDraft.points.length) {
      var points = state.polygonDraft.points.slice();
      if (state.polygonDraft.preview) {
        points.push(state.polygonDraft.preview);
      }
      ctx.save();
      ctx.strokeStyle = "#1d4ed8";
      ctx.fillStyle = "rgba(37, 99, 235, 0.12)";
      ctx.lineWidth = Math.max(1, 1 / state.zoom);
      ctx.beginPath();
      ctx.moveTo(points[0].x, points[0].y);
      points.slice(1).forEach(function (point) {
        ctx.lineTo(point.x, point.y);
      });
      if (state.polygonDraft.points.length >= 3 && !state.polygonDraft.preview) {
        ctx.closePath();
        ctx.fill();
      }
      ctx.stroke();
      points.forEach(function (point) {
        var radius = Math.max(3 / state.zoom, 2);
        ctx.beginPath();
        ctx.arc(point.x, point.y, radius, 0, Math.PI * 2);
        ctx.fillStyle = "#ffffff";
        ctx.fill();
        ctx.stroke();
      });
      ctx.restore();
    }
  }

  function getLayerBounds(layer) {
    return {
      x: layer.x || 0,
      y: layer.y || 0,
      width: Math.max(1, layer.width || 1),
      height: Math.max(1, layer.height || 1),
    };
  }

  function drawSelectionBox(ctx, layer) {
    var bounds = getLayerBounds(layer);
    ctx.save();
    ctx.strokeStyle = "#2563eb";
    ctx.lineWidth = Math.max(1, 1 / state.zoom);
    ctx.setLineDash([Math.max(4, 4 / state.zoom), Math.max(3, 3 / state.zoom)]);
    ctx.strokeRect(bounds.x, bounds.y, bounds.width, bounds.height);
    ctx.setLineDash([]);
    drawResizeHandles(ctx, bounds);
    ctx.restore();
  }

  function drawResizeHandles(ctx, bounds) {
    var size = Math.max(8 / state.zoom, 4);
    var half = size / 2;
    var handles = getResizeHandles(bounds);
    ctx.fillStyle = "#ffffff";
    ctx.strokeStyle = "#1d4ed8";
    ctx.lineWidth = Math.max(1, 1 / state.zoom);
    Object.keys(handles).forEach(function (key) {
      var handle = handles[key];
      ctx.fillRect(handle.x - half, handle.y - half, size, size);
      ctx.strokeRect(handle.x - half, handle.y - half, size, size);
    });
  }

  function getResizeHandles(bounds) {
    return {
      nw: { x: bounds.x, y: bounds.y },
      ne: { x: bounds.x + bounds.width, y: bounds.y },
      sw: { x: bounds.x, y: bounds.y + bounds.height },
      se: { x: bounds.x + bounds.width, y: bounds.y + bounds.height },
    };
  }

  function canvasPointFromEvent(event) {
    var rect = refs.overlayCanvas.getBoundingClientRect();
    var x = ((event.clientX - rect.left) / rect.width) * refs.overlayCanvas.width;
    var y = ((event.clientY - rect.top) / rect.height) * refs.overlayCanvas.height;
    return { x: x, y: y };
  }

  function pointInBounds(point, bounds) {
    return (
      point.x >= bounds.x &&
      point.x <= bounds.x + bounds.width &&
      point.y >= bounds.y &&
      point.y <= bounds.y + bounds.height
    );
  }

  function distanceToSegment(point, a, b) {
    var dx = b.x - a.x;
    var dy = b.y - a.y;
    if (dx === 0 && dy === 0) {
      return Math.hypot(point.x - a.x, point.y - a.y);
    }
    var t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / (dx * dx + dy * dy);
    var clamped = clamp(t, 0, 1);
    var x = a.x + clamped * dx;
    var y = a.y + clamped * dy;
    return Math.hypot(point.x - x, point.y - y);
  }

  function pointInPolygon(point, absolutePoints) {
    var inside = false;
    for (var i = 0, j = absolutePoints.length - 1; i < absolutePoints.length; j = i, i += 1) {
      var pi = absolutePoints[i];
      var pj = absolutePoints[j];
      var intersects =
        pi.y > point.y !== pj.y > point.y &&
        point.x < ((pj.x - pi.x) * (point.y - pi.y)) / (pj.y - pi.y) + pi.x;
      if (intersects) {
        inside = !inside;
      }
    }
    return inside;
  }

  function absoluteLayerPoints(layer) {
    return (layer.points || []).map(function (point) {
      return {
        x: layer.x + point.x,
        y: layer.y + point.y,
      };
    });
  }

  function hitTestLayer(point) {
    for (var i = state.project.layers.length - 1; i >= 0; i -= 1) {
      var layer = state.project.layers[i];
      if (layer.visible === false || layer.locked) {
        continue;
      }

      var bounds = getLayerBounds(layer);
      if (layer.type === "line" && layer.points && layer.points.length >= 2) {
        var points = absoluteLayerPoints(layer);
        if (distanceToSegment(point, points[0], points[1]) <= 8 / state.zoom) {
          return layer;
        }
      } else if (layer.type === "polygon" && layer.points && layer.points.length >= 3) {
        if (pointInPolygon(point, absoluteLayerPoints(layer)) || pointInBounds(point, bounds)) {
          return layer;
        }
      } else if (pointInBounds(point, bounds)) {
        return layer;
      }
    }
    return null;
  }

  function hitTestResizeHandle(point, layer) {
    if (!layer || layer.locked) {
      return null;
    }
    var tolerance = Math.max(8 / state.zoom, 4);
    var handles = getResizeHandles(getLayerBounds(layer));
    var best = null;
    var bestDistance = Infinity;
    Object.keys(handles).forEach(function (key) {
      var handle = handles[key];
      if (Math.abs(point.x - handle.x) <= tolerance && Math.abs(point.y - handle.y) <= tolerance) {
        var distance = Math.hypot(point.x - handle.x, point.y - handle.y);
        if (distance < bestDistance) {
          best = key;
          bestDistance = distance;
        }
      }
    });
    return best;
  }

  function startMove(layer, point) {
    state.drag = {
      mode: "move",
      layerId: layer.id,
      start: point,
      original: {
        x: layer.x,
        y: layer.y,
      },
    };
  }

  function startResize(layer, handle, point) {
    state.drag = {
      mode: "resize",
      layerId: layer.id,
      handle: handle,
      start: point,
      original: {
        x: layer.x,
        y: layer.y,
        width: layer.width,
        height: layer.height,
        points: layer.points ? layer.points.map(function (item) {
          return { x: item.x, y: item.y };
        }) : null,
      },
    };
  }

  function startDraw(tool, point) {
    state.drag = {
      mode: "draw",
      tool: tool,
      start: point,
      draftLayer: null,
    };
  }

  function updateDrag(point) {
    if (!state.drag) {
      return;
    }
    if (state.drag.mode === "draw") {
      state.drag.draftLayer = createShapeLayerFromDrag(state.drag.tool, state.drag.start, point);
      renderProject();
      return;
    }

    var layer = state.project.layers.find(function (item) {
      return item.id === state.drag.layerId;
    });
    if (!layer || layer.locked) {
      return;
    }

    var dx = point.x - state.drag.start.x;
    var dy = point.y - state.drag.start.y;

    if (state.drag.mode === "move") {
      layer.x = Math.round(state.drag.original.x + dx);
      layer.y = Math.round(state.drag.original.y + dy);
    }

    if (state.drag.mode === "resize") {
      applyResize(layer, state.drag, dx, dy);
    }

    renderProject();
    updateLayerControls();
  }

  function applyResize(layer, drag, dx, dy) {
    var original = drag.original;
    var left = original.x;
    var top = original.y;
    var right = original.x + original.width;
    var bottom = original.y + original.height;

    if (drag.handle.indexOf("w") !== -1) {
      left = original.x + dx;
    }
    if (drag.handle.indexOf("e") !== -1) {
      right = original.x + original.width + dx;
    }
    if (drag.handle.indexOf("n") !== -1) {
      top = original.y + dy;
    }
    if (drag.handle.indexOf("s") !== -1) {
      bottom = original.y + original.height + dy;
    }

    if (right < left) {
      var swapX = right;
      right = left;
      left = swapX;
    }
    if (bottom < top) {
      var swapY = bottom;
      bottom = top;
      top = swapY;
    }

    layer.x = Math.round(left);
    layer.y = Math.round(top);
    layer.width = Math.max(1, Math.round(right - left));
    layer.height = Math.max(1, Math.round(bottom - top));

    if (drag.original.points && drag.original.width && drag.original.height) {
      var scaleX = layer.width / drag.original.width;
      var scaleY = layer.height / drag.original.height;
      layer.points = drag.original.points.map(function (point) {
        return {
          x: Math.round(point.x * scaleX),
          y: Math.round(point.y * scaleY),
        };
      });
    }
  }

  function normalizeDragBounds(start, point) {
    var x = Math.min(start.x, point.x);
    var y = Math.min(start.y, point.y);
    var width = Math.abs(point.x - start.x);
    var height = Math.abs(point.y - start.y);
    return {
      x: Math.round(x),
      y: Math.round(y),
      width: Math.max(1, Math.round(width)),
      height: Math.max(1, Math.round(height)),
    };
  }

  function createShapeLayerFromDrag(tool, start, point) {
    if (tool === "line") {
      return createLineLayer(start, point);
    }

    var bounds = normalizeDragBounds(start, point);
    return Object.assign(
      {
        type: tool,
        name: nextLayerName(tool),
        visible: true,
        locked: false,
        fillGray: 15,
        strokeGray: 0,
        strokeWidth: 2,
      },
      bounds
    );
  }

  function createLineLayer(start, point) {
    var bounds = normalizeDragBounds(start, point);
    var startPoint = {
      x: Math.round(start.x - bounds.x),
      y: Math.round(start.y - bounds.y),
    };
    var endPoint = {
      x: Math.round(point.x - bounds.x),
      y: Math.round(point.y - bounds.y),
    };
    return Object.assign(
      {
        type: "line",
        name: nextLayerName("line"),
        visible: true,
        locked: false,
        fillGray: null,
        strokeGray: 0,
        strokeWidth: 2,
        points: [startPoint, endPoint],
      },
      bounds
    );
  }

  function commitDraftLayer() {
    if (!state.drag || state.drag.mode !== "draw" || !state.drag.draftLayer) {
      state.drag = null;
      renderProject();
      return;
    }

    var layer = state.drag.draftLayer;
    state.drag = null;
    if (layer.type === "line") {
      var points = absoluteLayerPoints(layer);
      if (points.length >= 2 && distanceToSegment(points[0], points[0], points[1]) >= 0) {
        if (Math.hypot(points[1].x - points[0].x, points[1].y - points[0].y) < 2) {
          renderProject();
          return;
        }
      }
    } else if (layer.width < 2 || layer.height < 2) {
      renderProject();
      return;
    }

    addLayer(layer);
    setActiveTool("select");
  }

  function normalizePolygon(points) {
    var xs = points.map(function (point) { return point.x; });
    var ys = points.map(function (point) { return point.y; });
    var minX = Math.min.apply(Math, xs);
    var minY = Math.min.apply(Math, ys);
    var maxX = Math.max.apply(Math, xs);
    var maxY = Math.max.apply(Math, ys);

    return {
      x: Math.round(minX),
      y: Math.round(minY),
      width: Math.max(1, Math.round(maxX - minX)),
      height: Math.max(1, Math.round(maxY - minY)),
      points: points.map(function (point) {
        return {
          x: Math.round(point.x - minX),
          y: Math.round(point.y - minY),
        };
      }),
    };
  }

  function finishPolygon(commit) {
    if (!state.polygonDraft) {
      return;
    }

    var draft = state.polygonDraft;
    state.polygonDraft = null;
    refs.finishPolygon.disabled = true;

    if (commit !== false && draft.points.length >= 3) {
      addLayer(
        Object.assign(
          {
            type: "polygon",
            name: nextLayerName("polygon"),
            visible: true,
            locked: false,
            fillGray: 15,
            strokeGray: 0,
            strokeWidth: 2,
          },
          normalizePolygon(draft.points)
        )
      );
    } else {
      renderProject();
    }
    setActiveTool("select");
  }

  function nextLayerName(type) {
    var base = layerTypeLabel(type);
    var count = state.project.layers.filter(function (layer) {
      return layer.type === type;
    }).length;
    return base + " " + (count + 1);
  }

  function setActiveTool(tool) {
    if (state.activeTool === "polygon" && tool !== "polygon" && state.polygonDraft) {
      finishPolygon(false);
    }
    state.activeTool = tool;
    updateToolButtons();
    refs.overlayCanvas.style.cursor = tool === "select" ? "default" : "crosshair";
  }

  function updateToolButtons() {
    [
      refs.toolSelect,
      refs.toolRect,
      refs.toolEllipse,
      refs.toolLine,
      refs.toolPolygon,
    ].forEach(function (button) {
      button.classList.toggle("active", button.dataset.tool === state.activeTool);
    });
    refs.finishPolygon.disabled = !state.polygonDraft || state.polygonDraft.points.length < 3;
  }

  function handlePointerDown(event) {
    var point = canvasPointFromEvent(event);
    if (state.activeTool === "polygon") {
      event.preventDefault();
      if (!state.polygonDraft) {
        state.polygonDraft = { points: [], preview: null };
      }
      state.polygonDraft.points.push({ x: Math.round(point.x), y: Math.round(point.y) });
      refs.finishPolygon.disabled = state.polygonDraft.points.length < 3;
      renderOverlay();
      return;
    }

    if (state.activeTool === "rect" || state.activeTool === "ellipse" || state.activeTool === "line") {
      event.preventDefault();
      startDraw(state.activeTool, point);
      refs.overlayCanvas.setPointerCapture(event.pointerId);
      return;
    }

    var selected = getSelectedLayer();
    var handle = hitTestResizeHandle(point, selected);
    if (handle) {
      event.preventDefault();
      startResize(selected, handle, point);
      refs.overlayCanvas.setPointerCapture(event.pointerId);
      return;
    }

    if (state.activeTool === "select") {
      var layer = hitTestLayer(point);
      selectLayer(layer ? layer.id : null);
      if (layer && !layer.locked) {
        event.preventDefault();
        startMove(layer, point);
        refs.overlayCanvas.setPointerCapture(event.pointerId);
      }
    }
  }

  function handlePointerMove(event) {
    if (state.activeTool === "polygon" && state.polygonDraft) {
      state.polygonDraft.preview = canvasPointFromEvent(event);
      renderOverlay();
      return;
    }

    if (state.drag) {
      event.preventDefault();
      updateDrag(canvasPointFromEvent(event));
    }
  }

  function handlePointerUp(event) {
    if (state.drag) {
      event.preventDefault();
      var drag = state.drag;
      if (drag.mode === "draw") {
        commitDraftLayer();
      } else {
        state.drag = null;
        renderProject();
        renderLayerList();
        updateLayerControls();
      }
      if (refs.overlayCanvas.hasPointerCapture(event.pointerId)) {
        refs.overlayCanvas.releasePointerCapture(event.pointerId);
      }
    }
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
    refs.imageInput.addEventListener("change", function () {
      handleImageImport(refs.imageInput.files && refs.imageInput.files[0]);
    });

    refs.overlayCanvas.addEventListener("pointerdown", handlePointerDown);
    refs.overlayCanvas.addEventListener("pointermove", handlePointerMove);
    refs.overlayCanvas.addEventListener("pointerup", handlePointerUp);
    refs.overlayCanvas.addEventListener("pointercancel", handlePointerUp);
    refs.overlayCanvas.addEventListener("dblclick", function () {
      if (state.activeTool === "polygon") {
        finishPolygon(true);
      }
    });

    [
      refs.toolSelect,
      refs.toolRect,
      refs.toolEllipse,
      refs.toolLine,
      refs.toolPolygon,
    ].forEach(function (button) {
      button.addEventListener("click", function () {
        setActiveTool(button.dataset.tool);
      });
    });
    refs.finishPolygon.addEventListener("click", function () {
      finishPolygon(true);
    });

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
    updateToolButtons();
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
    handleImageImport: handleImageImport,
    hitTestLayer: hitTestLayer,
    canvasPointFromEvent: canvasPointFromEvent,
  };

  document.addEventListener("DOMContentLoaded", init);
})();
