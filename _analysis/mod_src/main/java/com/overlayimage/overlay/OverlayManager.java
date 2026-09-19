package com.overlayimage.overlay;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.reflect.TypeToken;
import com.overlayimage.OverlayImageMod;
import com.overlayimage.gif.GifDecoder;
import net.fabricmc.loader.api.FabricLoader;

import java.io.IOException;
import java.io.Reader;
import java.io.Writer;
import java.lang.reflect.Type;
import java.nio.file.*;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public class OverlayManager {
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
    private final Path configDir;
    private final Path imagesDir;
    private final Path configFile;

    private final List<ImageOverlay> overlays = new ArrayList<>();
    private boolean dirty;
    private ImageOverlay selectedOverlay;
    private boolean interacting;
    private boolean paused;

    private boolean pickingWaypoints;
    private ImageOverlay waypointTarget;
    private final List<ImageOverlay.Waypoint> pendingWaypoints = new ArrayList<>();
    private boolean capturingKey;

    public OverlayManager() {
        configDir = FabricLoader.getInstance().getConfigDir().resolve("watermark");
        imagesDir = configDir.resolve("images");
        configFile = configDir.resolve("overlays.json");
        try { Files.createDirectories(imagesDir); } catch (IOException e) {
            OverlayImageMod.LOGGER.error("Failed to create config dirs", e);
        }
    }

    public void loadSavedOverlays() {
        if (!Files.exists(configFile)) return;
        try (Reader reader = Files.newBufferedReader(configFile)) {
            Type listType = new TypeToken<List<SavedOverlay>>(){}.getType();
            List<SavedOverlay> saved = GSON.fromJson(reader, listType);
            if (saved == null) return;
            for (SavedOverlay s : saved) {
                Path imgPath = imagesDir.resolve(s.fileName);
                if (!Files.exists(imgPath)) { OverlayImageMod.LOGGER.warn("Saved image not found: {}", imgPath); continue; }
                try {
                    GifDecoder.LoadResult result = GifDecoder.loadImage(imgPath.toString());
                    ImageOverlay overlay = new ImageOverlay(s.fileName, s.fileName, result.frames(), result.delays(), result.width(), result.height());
                    List<ImageOverlay.Waypoint> wps = new ArrayList<>();
                    if (s.waypoints != null) {
                        for (SavedWaypoint sw : s.waypoints) wps.add(new ImageOverlay.Waypoint(sw.xf, sw.yf));
                    }
                    overlay.setSavedState(s.xFraction, s.yFraction, s.scale,
                            MovementMode.valueOf(s.movementMode != null ? s.movementMode : "NONE"),
                            wps, s.currentWaypointIndex, s.randDX, s.randDY);
                    overlay.setVisible(s.visible);
                    overlay.setBoundKeyCode(s.boundKeyCode);
                    overlays.add(overlay);
                } catch (Exception e) { OverlayImageMod.LOGGER.error("Failed to load saved image: {}", s.fileName, e); }
            }
        } catch (Exception e) { OverlayImageMod.LOGGER.error("Failed to load config", e); }
    }

    private long lastSaveTime;
    private static final long SAVE_INTERVAL_MS = 1000;

    public void saveOverlays() {
        long now = System.currentTimeMillis();
        if (now - lastSaveTime < SAVE_INTERVAL_MS) { dirty = true; return; }
        actuallySave();
        lastSaveTime = now;
        dirty = false;
    }

    private void actuallySave() {
        List<SavedOverlay> saved = new ArrayList<>();
        for (ImageOverlay o : overlays) {
            List<SavedWaypoint> swps = new ArrayList<>();
            for (ImageOverlay.Waypoint w : o.getWaypoints()) swps.add(new SavedWaypoint(w.xFraction(), w.yFraction()));
            saved.add(new SavedOverlay(o.getBackupFileName(), o.getXFraction(), o.getYFraction(), o.getScale(),
                    o.getMovementMode().name(), swps, o.getCurrentWaypointIndex(), o.getRandMoveDX(), o.getRandMoveDY(),
                    o.isVisible(), o.getBoundKeyCode()));
        }
        try (Writer writer = Files.newBufferedWriter(configFile)) { GSON.toJson(saved, writer); }
        catch (Exception e) { OverlayImageMod.LOGGER.error("Failed to save config", e); }
    }

    public void flushPendingSave() {
        if (dirty) { actuallySave(); lastSaveTime = System.currentTimeMillis(); dirty = false; }
    }

    public String copyImageToBackup(String sourcePath) throws IOException {
        Path source = Paths.get(sourcePath);
        String name = source.getFileName().toString();
        int dot = name.lastIndexOf('.'); String ext = dot >= 0 ? name.substring(dot) : "";
        String uuid = UUID.randomUUID().toString().substring(0, 8);
        String backupName = uuid + ext;
        Files.copy(source, imagesDir.resolve(backupName), StandardCopyOption.REPLACE_EXISTING);
        return backupName;
    }

    public Path getConfigDir() { return configDir; }
    public Path getImagesDir() { return imagesDir; }

    public void addOverlay(ImageOverlay overlay) { overlays.add(overlay); saveOverlays(); }
    public void removeOverlay(ImageOverlay overlay) {
        overlays.remove(overlay); overlay.close();
        if (selectedOverlay == overlay) selectedOverlay = null;
        saveOverlays();
    }
    public void clearAll() { for (ImageOverlay o : overlays) o.close(); overlays.clear(); selectedOverlay = null; }

    public void syncFractions(int screenW, int screenH) {
        for (ImageOverlay o : overlays) o.syncFractionsFromPixels(screenW, screenH);
        saveOverlays();
    }

    public void tickMovement(int screenW, int screenH) {
        for (ImageOverlay o : overlays) o.tickMovement(screenW, screenH);
        flushPendingSave();
    }

    public List<ImageOverlay> getOverlays() { return overlays; }
    public ImageOverlay getSelectedOverlay() { return selectedOverlay; }
    public void setSelectedOverlay(ImageOverlay o) { this.selectedOverlay = o; }
    public boolean isInteracting() { return interacting; }
    public void setInteracting(boolean v) { this.interacting = v; }
    public boolean isPaused() { return paused; }
    public void setPaused(boolean p) { this.paused = p; }

    public boolean isPickingWaypoints() { return pickingWaypoints; }
    public ImageOverlay getWaypointTarget() { return waypointTarget; }
    public List<ImageOverlay.Waypoint> getPendingWaypoints() { return pendingWaypoints; }

    public void startWaypointPicking(ImageOverlay target) {
        pickingWaypoints = true; waypointTarget = target; pendingWaypoints.clear();
    }
    public void addWaypoint(double xf, double yf) {
        if (pendingWaypoints.size() < 7) pendingWaypoints.add(new ImageOverlay.Waypoint(xf, yf));
    }
    public void removeLastWaypoint() {
        if (!pendingWaypoints.isEmpty()) pendingWaypoints.remove(pendingWaypoints.size() - 1);
    }
    public void confirmWaypoints(int screenW, int screenH) {
        if (waypointTarget == null || pendingWaypoints.isEmpty()) { cancelWaypoints(); return; }
        waypointTarget.getWaypoints().clear();
        waypointTarget.getWaypoints().addAll(pendingWaypoints);
        waypointTarget.setMovementMode(MovementMode.WAYPOINT);
        var first = pendingWaypoints.get(0);
        waypointTarget.setSavedState(first.xFraction(), first.yFraction(), waypointTarget.getScale(),
                MovementMode.WAYPOINT, new ArrayList<>(pendingWaypoints), 0,
                waypointTarget.getRandMoveDX(), waypointTarget.getRandMoveDY());
        waypointTarget.updateFromScreenSize(screenW, screenH);
        pendingWaypoints.clear();
        pickingWaypoints = false; waypointTarget = null;
        saveOverlays();
    }
    public void cancelWaypoints() {
        pendingWaypoints.clear(); pickingWaypoints = false; waypointTarget = null;
    }

    public boolean isCapturingKey() { return capturingKey; }
    public void startKeyCapture(ImageOverlay target) { capturingKey = true; keyCaptureTarget = target; }
    public void stopKeyCapture() { capturingKey = false; keyCaptureTarget = null; }
    public void setCapturedKey(int key) {
        if (keyCaptureTarget != null) {
            keyCaptureTarget.setBoundKeyCode(key);
            saveOverlays();
        }
    }
    private ImageOverlay keyCaptureTarget;

    public ImageOverlay getOverlayAt(int mx, int my) {
        for (int i = overlays.size() - 1; i >= 0; i--) {
            ImageOverlay ov = overlays.get(i);
            if (ov.containsPoint(mx, my) && (ov.isVisible() || interacting)) return ov;
        }
        return null;
    }

    public ImageOverlay getOverlayAtCorner(int mx, int my, int cs) {
        for (int i = overlays.size() - 1; i >= 0; i--) {
            ImageOverlay ov = overlays.get(i);
            if (ov.isOverCorner(mx, my, cs) && (ov.isVisible() || interacting)) return ov;
        }
        return null;
    }

    static class SavedOverlay {
        String fileName; double xFraction, yFraction; float scale;
        String movementMode; List<SavedWaypoint> waypoints; int currentWaypointIndex; double randDX, randDY;
        boolean visible = true; int boundKeyCode;
        SavedOverlay(String fn, double xf, double yf, float s, String mm, List<SavedWaypoint> wps, int wi, double rdx, double rdy,
                     boolean vis, int bkc) {
            this.fileName = fn; this.xFraction = xf; this.yFraction = yf; this.scale = s;
            this.movementMode = mm; this.waypoints = wps; this.currentWaypointIndex = wi; this.randDX = rdx; this.randDY = rdy;
            this.visible = vis; this.boundKeyCode = bkc;
        }
    }
    static class SavedWaypoint { double xf; double yf;
        SavedWaypoint(double x, double y) { this.xf = x; this.yf = y; }
    }
}
