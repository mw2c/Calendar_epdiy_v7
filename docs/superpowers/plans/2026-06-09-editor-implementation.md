# Web 16 灰阶图片编辑器 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a static WYSIWYG 16-grayscale image editor in `editor/` for ED060KD1 electronic-paper image asset creation.

**Architecture:** The editor is a no-build Vanilla JS app. `index.html` provides the three-column workbench, `styles.css` provides the desktop editing UI, `app.js` owns state/rendering/import/export/project serialization, and `README.md` documents use and file formats.

**Tech Stack:** HTML Canvas 2D, browser File APIs, Blob downloads, plain CSS, plain JavaScript.

---

## File Structure

- Create `editor/index.html`: static page shell, controls, canvas host, layer list, property panel, file inputs.
- Create `editor/styles.css`: three-column workbench, toolbar controls, canvas viewport, layer/property styling, responsive fallback.
- Create `editor/app.js`: project state, layer model, rendering, 16-gray quantization, pointer editing, image import, shape tools, project import/export, binary/BMP export.
- Create `editor/README.md`: usage instructions, supported files, project JSON format, `bin/raw/bmp` byte layout.

No ESP-IDF files, `main/`, `components/`, `sdkconfig`, or build outputs should be modified.

## Task 1: Static Editor Shell

**Files:**
- Create: `editor/index.html`
- Create: `editor/styles.css`

- [ ] **Step 1: Create `editor/` and write the HTML shell**

`editor/index.html` must include:

- A header with project title, project save/import buttons, and export buttons for `bin`, `raw`, `bmp`.
- A left sidebar with screen preset select, custom width/height inputs, image import input, and tool buttons for select, rectangle, ellipse, line, polygon.
- A center canvas area with one visible `<canvas id="displayCanvas">`, one overlay `<canvas id="overlayCanvas">`, zoom controls, and a status line.
- A right sidebar with selected element properties and a layer list.
- Hidden file inputs for image and project import.
- Script include: `<script src="app.js" defer></script>`.

Use semantic IDs because `app.js` will bind to them:

```html
screenPreset, canvasWidth, canvasHeight, applyScreenSize,
imageInput, projectInput, saveProject, importProject,
exportBin, exportRaw, exportBmp,
toolSelect, toolRect, toolEllipse, toolLine, toolPolygon, finishPolygon,
zoomOut, zoomLevel, zoomIn, fitCanvas,
displayCanvas, overlayCanvas, canvasStatus,
selectedPanel, noSelection, layerName, propX, propY, propWidth, propHeight,
fillGray, strokeGray, strokeWidth, opacity, deleteLayer,
moveLayerUp, moveLayerDown, layerList, message
```

- [ ] **Step 2: Write CSS for a three-column workbench**

`editor/styles.css` must define:

- Full viewport app layout.
- Header controls and buttons with compact desktop-tool styling.
- Left and right sidebars with fixed widths around 260-320 px.
- Center canvas area with scrollable viewport and checker/neutral background.
- Canvas stack positioning so display and overlay canvases align exactly.
- Layer rows with active, hidden, and locked states.
- Form controls that fit labels and numeric inputs without text overlap.

- [ ] **Step 3: Open the static shell**

Run:

```bash
python3 -m http.server 8765 --directory editor
```

Expected: server starts and serves `http://localhost:8765`.

- [ ] **Step 4: Verify shell visually**

Open `http://localhost:8765`. Expected:

- Three columns are visible.
- The central canvas area is present.
- Buttons and inputs do not overlap.
- Browser console has no missing file errors.

- [ ] **Step 5: Commit**

```bash
git add editor/index.html editor/styles.css
git commit -m "Add editor static shell"
```

## Task 2: Project State and 16-Gray Rendering

**Files:**
- Create: `editor/app.js`
- Modify: `editor/index.html`
- Modify: `editor/styles.css` only if rendering needs minor layout fixes.

- [ ] **Step 1: Initialize state and DOM bindings**

Create `editor/app.js` with:

- `SCREEN_PRESETS` containing `{ id: "ed060kd1-portrait", label: "ED060KD1 portrait", width: 1072, height: 1448 }`.
- `state.project` with `version`, `screenPreset`, `canvas`, and `layers`.
- `state.selectedId`, `state.activeTool`, `state.zoom`, `state.drag`, `state.polygonDraft`.
- `refs` object that binds every ID listed in Task 1.
- `showMessage(text, kind)` for UI feedback.
- `createDefaultProject()` returning a white ED060KD1 portrait project.

- [ ] **Step 2: Add canvas sizing and zoom**

Implement:

```js
function resizeCanvases(width, height) {}
function updateCanvasCssSize() {}
function setZoom(nextZoom) {}
function fitCanvasToViewport() {}
```

Expected behavior:

- Backing canvas dimensions always equal project `width` and `height`.
- CSS size is `width * zoom` and `height * zoom`.
- Zoom is clamped to `0.1..4`.
- Fit computes a zoom that fits the canvas stack inside the center viewport.

- [ ] **Step 3: Implement 16-gray render pipeline**

Implement:

```js
function renderProject() {}
function drawLayer(ctx, layer) {}
function quantizeCanvasToGray16(sourceCanvas) {}
function paintGray16ToDisplay(grayPixels, width, height) {}
function grayToCss(gray) {}
function grayToByte(gray) {}
```

Rules:

- Clear hidden canvas to white.
- Draw visible unlocked and locked layers in order.
- Convert RGBA to luminance using `0.299 * r + 0.587 * g + 0.114 * b`, composited over white.
- Quantize with `Math.round(luminance / 17)` and clamp to `0..15`.
- Paint display pixels back to visible canvas using `gray * 17`.
- `0` is black and `15` is white.

- [ ] **Step 4: Render empty default project**

Wire `init()` on DOMContentLoaded:

- Create default project.
- Size canvases to `1072 x 1448`.
- Fit the canvas.
- Render a white quantized canvas.
- Show status `1072 x 1448 · 16 灰阶`.

- [ ] **Step 5: Verify default render**

Open `http://localhost:8765`. Expected:

- Canvas is visible as a white portrait page.
- Status displays `1072 x 1448`.
- Console has no runtime errors.

- [ ] **Step 6: Commit**

```bash
git add editor/app.js editor/index.html editor/styles.css
git commit -m "Add editor render pipeline"
```

## Task 3: Screen Size, Layer List, and Property Panel

**Files:**
- Modify: `editor/app.js`
- Modify: `editor/index.html` only if property fields need missing controls.
- Modify: `editor/styles.css` only for panel state fixes.

- [ ] **Step 1: Implement screen-size controls**

Add handlers:

```js
function applyScreenSizeFromControls() {}
function setScreenSize(width, height, presetId) {}
function validateCanvasSize(width, height) {}
```

Rules:

- Width and height must be positive integers.
- Maximum width and height are `4096`.
- Changing size keeps existing layers but clamps no coordinates; off-canvas content is allowed and clipped by render.
- ED060KD1 preset writes `1072` and `1448`.

- [ ] **Step 2: Implement layer selection and list rendering**

Add:

```js
function getSelectedLayer() {}
function selectLayer(id) {}
function renderLayerList() {}
function updateLayerControls() {}
function moveSelectedLayer(delta) {}
function deleteSelectedLayer() {}
```

Layer list rows must include:

- Layer name.
- Type label.
- Visibility toggle.
- Lock toggle.
- Active selection state.

- [ ] **Step 3: Implement property editing**

Bind `input` or `change` events for:

- `layerName`
- `propX`
- `propY`
- `propWidth`
- `propHeight`
- `fillGray`
- `strokeGray`
- `strokeWidth`
- `opacity`

Rules:

- Numeric position and size fields round to integers.
- Size fields clamp to at least `1`.
- Gray fields clamp to `0..15`.
- Opacity clamps to `0..1`.
- Irrelevant controls are disabled for layer types that do not use them.

- [ ] **Step 4: Verify controls on synthetic layers**

Temporarily create one rectangle layer inside `init()` during this step, verify:

- It appears in layer list.
- Selecting it populates property fields.
- Editing `x/y/width/height/fillGray/strokeGray/strokeWidth` redraws the canvas.

Remove the temporary layer before committing.

- [ ] **Step 5: Commit**

```bash
git add editor/app.js editor/index.html editor/styles.css
git commit -m "Add editor layer controls"
```

## Task 4: Image Import, Selection, Drag, and Resize

**Files:**
- Modify: `editor/app.js`
- Modify: `editor/styles.css` if selection handles need styling support.

- [ ] **Step 1: Implement image loading**

Add:

```js
function handleImageImport(file) {}
function readFileAsDataURL(file) {}
function loadImage(src) {}
function createImageLayer(src, image) {}
```

Rules:

- Accept MIME types `image/svg+xml`, `image/png`, `image/jpeg`.
- Reject unsupported types with `showMessage("仅支持 SVG、PNG、JPG 图片", "error")`.
- Fit imported images within 80% of canvas width and height while preserving aspect ratio.
- Center imported image.
- Name layer using the file name.
- Cache decoded images by layer ID for rendering.

- [ ] **Step 2: Implement hit testing**

Add:

```js
function canvasPointFromEvent(event) {}
function hitTestLayer(point) {}
function hitTestResizeHandle(point, layer) {}
function getLayerBounds(layer) {}
```

Rules:

- Iterate layers from top to bottom.
- Ignore invisible or locked layers for direct manipulation.
- Rect/image/ellipse use bounding boxes for hit testing.
- Line uses distance to segment with tolerance `8 / zoom`.
- Polygon uses point-in-polygon with bounding-box fallback while drawing.

- [ ] **Step 3: Implement pointer editing**

Bind pointer events on `overlayCanvas`:

```js
function handlePointerDown(event) {}
function handlePointerMove(event) {}
function handlePointerUp(event) {}
function startMove(layer, point) {}
function startResize(layer, handle, point) {}
function updateDrag(point) {}
```

Rules:

- Select tool selects topmost hit layer.
- Dragging selected layer moves `x` and `y`.
- Corner handles resize selected layer.
- Resizing from west or north updates `x` or `y` and size.
- Width and height clamp to at least `1`.
- Locked layers cannot be moved or resized.

- [ ] **Step 4: Draw overlay selection**

Add:

```js
function renderOverlay() {}
function drawSelectionBox(ctx, layer) {}
function drawResizeHandles(ctx, bounds) {}
```

Expected:

- Selected layer shows a visible bounding box.
- Four corner handles remain stable while zooming.
- Overlay clears when no layer is selected.

- [ ] **Step 5: Verify image workflow**

Manual checks:

- Import PNG.
- Import SVG.
- Select imported image.
- Drag image.
- Resize from each corner.
- Edit exact position and size in right panel.
- Toggle lock and verify drag stops.

- [ ] **Step 6: Commit**

```bash
git add editor/app.js editor/styles.css
git commit -m "Add editor image manipulation"
```

## Task 5: Shape Tools

**Files:**
- Modify: `editor/app.js`
- Modify: `editor/index.html` if shape defaults need extra controls.

- [ ] **Step 1: Implement tool switching**

Add:

```js
function setActiveTool(tool) {}
function updateToolButtons() {}
function nextLayerName(type) {}
```

Rules:

- Active button is visually marked.
- Switching away from polygon cancels unfinished polygon only after confirmation through `finishPolygon(false)` behavior.
- Select tool remains default.

- [ ] **Step 2: Implement rectangle and ellipse creation**

Rules:

- With rect or ellipse tool active, pointer down starts a draft layer.
- Pointer move updates width and height from drag distance.
- Pointer up commits the layer when width and height are at least `2`.
- Negative drag direction normalizes `x/y/width/height`.
- Defaults: fill gray `15`, stroke gray `0`, stroke width `2`.

- [ ] **Step 3: Implement line creation**

Rules:

- Pointer down records start point.
- Pointer move updates end point.
- Pointer up commits line if endpoint differs from start by at least `2` pixels.
- Line layer stores `points: [{x:0,y:0},{x:width,y:height}]` relative to `x/y`.
- Defaults: stroke gray `0`, stroke width `2`, no fill.

- [ ] **Step 4: Implement polygon creation**

Rules:

- With polygon tool active, each click adds one point in canvas coordinates.
- Draft line follows pointer to preview the next segment.
- Double click or `finishPolygon` button commits polygon when it has at least 3 points.
- Committed polygon normalizes points relative to its bounding box and stores `x/y/width/height/points`.
- Defaults: fill gray `15`, stroke gray `0`, stroke width `2`.

- [ ] **Step 5: Extend shape rendering**

Ensure `drawLayer(ctx, layer)` renders:

- `rect`: fill then stroke rectangle.
- `ellipse`: fill then stroke ellipse.
- `line`: stroke line with round caps.
- `polygon`: fill then stroke closed polygon.

All shape gray values must use `grayToCss`.

- [ ] **Step 6: Verify shape workflow**

Manual checks:

- Create rectangle, ellipse, line, polygon.
- Select each after creation.
- Drag and resize each shape.
- Change fill/stroke/line width.
- Reorder layers and verify visual stacking.

- [ ] **Step 7: Commit**

```bash
git add editor/app.js editor/index.html
git commit -m "Add editor shape tools"
```

## Task 6: Project Save and Import

**Files:**
- Modify: `editor/app.js`

- [ ] **Step 1: Implement project serialization**

Add:

```js
function serializeProject() {}
function downloadBlob(blob, filename) {}
function saveProjectToFile() {}
```

Rules:

- Output pretty JSON with two-space indentation.
- Include `version`, `screenPreset`, `canvas`, and all `layers`.
- Do not serialize runtime image cache, drag state, selected layer, or DOM state.
- Filename format: `epd-editor-project-YYYYMMDD-HHMMSS.json`.

- [ ] **Step 2: Implement project import validation**

Add:

```js
function handleProjectImport(file) {}
function parseProjectJson(text) {}
function validateProject(project) {}
function loadProject(project) {}
function rebuildImageCache() {}
```

Rules:

- Parse into a temporary object first.
- Accept only `version: 1`.
- Require positive integer `canvas.width` and `canvas.height`.
- Require `layers` array.
- Validate each layer type and required fields.
- Rebuild decoded image cache before replacing current project.
- If any image fails to decode, reject the import and keep current project unchanged.

- [ ] **Step 3: Verify project round trip**

Manual checks:

- Build a project with one imported image and all four shape types.
- Save JSON.
- Reload page.
- Import JSON.
- Confirm dimensions, layers, names, visibility, lock states, positions, gray values, and visual stacking are restored.

- [ ] **Step 4: Commit**

```bash
git add editor/app.js
git commit -m "Add editor project import export"
```

## Task 7: Binary, Raw, and BMP Export

**Files:**
- Modify: `editor/app.js`
- Modify: `editor/index.html` if export status needs a display node.

- [ ] **Step 1: Expose final grayscale pixels**

Add:

```js
function renderToGrayPixels() {}
```

Rules:

- Reuse the same hidden-canvas render path used by display rendering.
- Return `{ width, height, pixels }`.
- `pixels` is `Uint8Array` with one value per pixel in range `0..15`.

- [ ] **Step 2: Implement packed 4bpp export**

Add:

```js
function packGray4bpp(width, height, pixels) {}
function exportPacked(extension) {}
```

Rules:

- Output length is `Math.ceil(width / 2) * height`.
- Each byte stores left pixel in high nibble and right pixel in low nibble.
- Odd-width final low nibble is `0`.
- `bin` and `raw` both call this packer in first version.

- [ ] **Step 3: Implement BMP export**

Add:

```js
function createGrayBmp(width, height, pixels) {}
function writeUint16LE(view, offset, value) {}
function writeUint32LE(view, offset, value) {}
```

Rules:

- Use BMP file header plus BITMAPINFOHEADER.
- Use 8-bit indexed color.
- Write 256 grayscale palette entries.
- Pixel values map from `0..15` to `0,17,34,...,255`.
- Write rows bottom-up.
- Pad each BMP row to a 4-byte boundary.

- [ ] **Step 4: Wire export buttons**

Expected filenames:

- `epd-image-<width>x<height>.bin`
- `epd-image-<width>x<height>.raw`
- `epd-image-<width>x<height>.bmp`

Show a message after export:

```text
已导出 <format>，尺寸 <width>x<height>
```

- [ ] **Step 5: Verify export lengths**

With default `1072 x 1448`, export `bin` and `raw`.

Expected:

```text
776128 bytes
```

Also export BMP and open it in an image viewer or browser tab. Expected: it visually matches the editor canvas.

- [ ] **Step 6: Commit**

```bash
git add editor/app.js editor/index.html
git commit -m "Add editor image exports"
```

## Task 8: Documentation and Final Verification

**Files:**
- Create: `editor/README.md`
- Modify: `editor/index.html`, `editor/styles.css`, or `editor/app.js` only for issues found during verification.

- [ ] **Step 1: Write `editor/README.md`**

The README must include:

- How to open the editor directly.
- How to run a local static server.
- Default screen: `ED060KD1 portrait 1072 x 1448`.
- Supported imports: SVG, PNG, JPG.
- Project JSON is self-contained and stores images as data URLs.
- Grayscale convention: `0 = black`, `15 = white`.
- `bin` byte layout.
- `raw` first-version layout equals `bin`.
- `bmp` is 8-bit grayscale for preview.

- [ ] **Step 2: Run static server**

Run:

```bash
python3 -m http.server 8765 --directory editor
```

Expected: server starts. If port is in use, use `8766` and record the actual URL in final notes.

- [ ] **Step 3: Browser verification**

Use Browser or a local browser to verify:

- Page loads without console errors.
- ED060KD1 preset applies `1072 x 1448`.
- Custom size changes canvas dimensions.
- PNG, SVG, and JPG imports work.
- Rectangle, ellipse, line, and polygon tools create editable layers.
- Drag, resize, property editing, visibility, lock, layer order, and delete work.
- Project save and import restore the scene.
- `bin`, `raw`, and `bmp` exports download.

- [ ] **Step 4: Check generated binary length**

For default ED060KD1 size:

```text
bin/raw length = ceil(1072 / 2) * 1448 = 776128 bytes
```

If download inspection is awkward in browser automation, add a temporary console call or use the browser debugger to run:

```js
packGray4bpp(1072, 1448, new Uint8Array(1072 * 1448).fill(15)).byteLength
```

Expected: `776128`.

- [ ] **Step 5: Final git status check**

Run:

```bash
git status --short
```

Expected: only intentional editor/docs changes are present. Do not add `.superpowers/` brainstorming artifacts.

- [ ] **Step 6: Commit docs and verification fixes**

```bash
git add editor/README.md editor/index.html editor/styles.css editor/app.js
git commit -m "Document editor usage"
```

If no files changed after Task 7 except README, commit only README.

## Self-Review

- Spec coverage: Tasks cover static architecture, ED060KD1/custom screen sizes, image import, shape drawing, post-add editing, project save/import, `bin/raw/bmp` export, error handling, docs, and manual verification.
- Testing approach: No unit-test TDD is required because this is a strong UI and pointer-interaction task. Verification is browser-based plus deterministic export-length checks, matching project instructions.
- Placeholder scan: This plan contains no unresolved placeholders or undefined future tasks.
- Type consistency: The planned project fields match the approved design: `version`, `screenPreset`, `canvas`, `layers`, common layer fields, image `src/sourceWidth/sourceHeight/opacity`, shape `fillGray/strokeGray/strokeWidth/points`.
