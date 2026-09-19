package com.overlayimage.overlay;

import net.minecraft.client.texture.NativeImageBackedTexture;
import net.minecraft.util.math.MathHelper;
import org.lwjgl.glfw.GLFW;

import java.io.Closeable;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

public class ImageOverlay implements Closeable {
    private static final Random RAND = new Random();
    private static final double MOVE_SPEED = 2.0;

    private final String backupFileName;
    private final String originalFileName;
    private final boolean animated;
    private final List<NativeImageBackedTexture> frames;
    private final List<Integer> frameDelays;
    private final int totalFrames;
    private final int originalWidth;
    private final int originalHeight;

    private double xFraction;
    private double yFraction;
    private float scale;
    private boolean dragging;
    private int dragOffsetX, dragOffsetY;
    private boolean resizing;

    private int cachedScreenW, cachedScreenH;
    private int cachedX, cachedY;
    private int cachedDisplayW, cachedDisplayH;

    private boolean needsTextureRegistration;
    private int currentFrameIndex;
    private long frameStartMs;
    private int animationLoops;

    private MovementMode movementMode = MovementMode.NONE;
    private boolean visible = true;
    private int boundKeyCode = GLFW.GLFW_KEY_UNKNOWN;
    private final List<Waypoint> waypoints = new ArrayList<>();
    private int currentWaypointIndex;
    private double randMoveDX, randMoveDY;
    private long waypointStaticUntil;
    private int randTargetX, randTargetY;

    public ImageOverlay(String backupFileName, String originalFileName,
                        List<NativeImageBackedTexture> frames, List<Integer> frameDelays,
                        int originalWidth, int originalHeight) {
        this.backupFileName = backupFileName;
        this.originalFileName = originalFileName;
        this.frames = frames;
        this.frameDelays = frameDelays;
        this.totalFrames = frames.size();
        this.animated = totalFrames > 1;
        this.originalWidth = originalWidth;
        this.originalHeight = originalHeight;
        this.scale = 1.0f;
        this.xFraction = 0.1;
        this.yFraction = 0.1;
        this.needsTextureRegistration = true;
        this.currentFrameIndex = 0;
        this.frameStartMs = System.currentTimeMillis();
        this.animationLoops = 0;
        randomizeMoveDirection();
    }

    public void setSavedState(double xFraction, double yFraction, float scale,
                               MovementMode mode, List<Waypoint> wps, int wpIndex,
                               double dmx, double dmy) {
        this.xFraction = MathHelper.clamp(xFraction, 0.0, 1.0);
        this.yFraction = MathHelper.clamp(yFraction, 0.0, 1.0);
        this.scale = MathHelper.clamp(scale, 0.1f, 10.0f);
        this.movementMode = mode;
        this.waypoints.clear();
        if (wps != null) this.waypoints.addAll(wps);
        this.currentWaypointIndex = wpIndex;
        this.randMoveDX = dmx;
        this.randMoveDY = dmy;
        if (randMoveDX == 0 && randMoveDY == 0) randomizeMoveDirection();
        this.cachedScreenW = 0;
        this.cachedScreenH = 0;
        if (mode == MovementMode.WAYPOINT) {
            this.waypointStaticUntil = System.currentTimeMillis() + 2000;
        }
    }

    public double getXFraction() { return xFraction; }
    public double getYFraction() { return yFraction; }
    public float getScale() { return scale; }
    public int getOriginalWidth() { return originalWidth; }
    public int getOriginalHeight() { return originalHeight; }

    public MovementMode getMovementMode() { return movementMode; }
    public void setMovementMode(MovementMode m) { this.movementMode = m; }
    public boolean isVisible() { return visible; }
    public void setVisible(boolean v) { this.visible = v; }
    public void toggleVisible() { this.visible = !this.visible; }
    public int getBoundKeyCode() { return boundKeyCode; }
    public void setBoundKeyCode(int k) { this.boundKeyCode = k; }
    public List<Waypoint> getWaypoints() { return waypoints; }
    public int getCurrentWaypointIndex() { return currentWaypointIndex; }
    public double getRandMoveDX() { return randMoveDX; }
    public double getRandMoveDY() { return randMoveDY; }

    public void updateFromScreenSize(int screenW, int screenH) {
        if (screenW == cachedScreenW && screenH == cachedScreenH) return;
        cachedScreenW = screenW;
        cachedScreenH = screenH;
        cachedX = (int) (xFraction * screenW);
        cachedY = (int) (yFraction * screenH);
        cachedDisplayW = (int) (originalWidth * scale);
        cachedDisplayH = (int) (originalHeight * scale);
    }

    public void syncFractionsFromPixels(int screenW, int screenH) {
        this.xFraction = (double) cachedX / screenW;
        this.yFraction = (double) cachedY / screenH;
        this.xFraction = MathHelper.clamp(this.xFraction, 0.0, 1.0);
        this.yFraction = MathHelper.clamp(this.yFraction, 0.0, 1.0);
    }

    public int getX() { return cachedX; }
    public int getY() { return cachedY; }
    public int getDisplayWidth() { return cachedDisplayW; }
    public int getDisplayHeight() { return cachedDisplayH; }

    public void setPosition(int x, int y) {
        this.cachedX = x;
        this.cachedY = y;
    }

    public void setScale(float scale) {
        this.scale = MathHelper.clamp(scale, 0.1f, 10.0f);
        this.cachedDisplayW = (int) (originalWidth * this.scale);
        this.cachedDisplayH = (int) (originalHeight * this.scale);
    }

    public boolean isAnimated() { return animated; }
    public int getTotalFrames() { return totalFrames; }
    public List<Integer> getFrameDelays() { return frameDelays; }
    public String getBackupFileName() { return backupFileName; }
    public String getOriginalFileName() { return originalFileName; }
    public NativeImageBackedTexture getFrame(int index) {
        return frames.get(index % Math.max(1, totalFrames));
    }

    public boolean isDragging() { return dragging; }
    public void startDrag(int mouseX, int mouseY) {
        this.dragging = true;
        this.dragOffsetX = mouseX - cachedX;
        this.dragOffsetY = mouseY - cachedY;
        this.resizing = false;
    }
    public void stopDrag() { this.dragging = false; this.resizing = false; }
    public void dragTo(int mouseX, int mouseY) {
        this.cachedX = mouseX - dragOffsetX;
        this.cachedY = mouseY - dragOffsetY;
    }
    public boolean isResizing() { return resizing; }
    public void startResize() { this.resizing = true; this.dragging = false; }
    public void resizeTo(int mouseX) {
        int newW = mouseX - cachedX;
        if (newW < 20) newW = 20;
        this.cachedDisplayW = newW;
        this.cachedDisplayH = (int) (newW / ((double) originalWidth / originalHeight));
        this.scale = (float) cachedDisplayW / originalWidth;
    }

    public boolean containsPoint(int mouseX, int mouseY) {
        return mouseX >= cachedX && mouseX <= cachedX + cachedDisplayW &&
               mouseY >= cachedY && mouseY <= cachedY + cachedDisplayH;
    }
    public boolean isOverCorner(int mouseX, int mouseY, int cornerSize) {
        int half = cornerSize / 2;
        return Math.abs(mouseX - (cachedX + cachedDisplayW)) <= half &&
               Math.abs(mouseY - (cachedY + cachedDisplayH)) <= half;
    }

    public boolean needsTextureRegistration() { return needsTextureRegistration; }
    public void markTexturesRegistered() { this.needsTextureRegistration = false; }

    public int getCurrentFrameIndex() {
        if (!animated) return 0;
        long now = System.currentTimeMillis();
        int delay = getFrameDelayMs(currentFrameIndex);
        if (delay > 0 && now - frameStartMs >= delay) {
            int prev = currentFrameIndex;
            currentFrameIndex = (currentFrameIndex + 1) % totalFrames;
            frameStartMs = now;
            if (currentFrameIndex < prev) animationLoops++;
        }
        return currentFrameIndex;
    }

    public int consumeAnimationLoopCount() {
        int c = animationLoops;
        animationLoops = 0;
        return c;
    }

    private int getFrameDelayMs(int index) {
        if (index < 0 || index >= frameDelays.size()) return 100;
        int d = frameDelays.get(index);
        return d > 10 ? d : 100;
    }

    public void tickMovement(int screenW, int screenH) {
        if (movementMode == MovementMode.NONE || !visible) return;
        int imgW = cachedDisplayW;
        int imgH = cachedDisplayH;

        switch (movementMode) {
            case WAYPOINT -> {
                if (waypoints.isEmpty()) break;
                Waypoint wp = waypoints.get(currentWaypointIndex);
                cachedX = (int)(wp.xFraction() * screenW);
                cachedY = (int)(wp.yFraction() * screenH);
                syncFractionsFromPixels(screenW, screenH);

                boolean advance;
                if (animated) {
                    advance = consumeAnimationLoopCount() > 0;
                } else {
                    advance = System.currentTimeMillis() >= waypointStaticUntil;
                }
                if (advance) {
                    currentWaypointIndex = (currentWaypointIndex + 1) % waypoints.size();
                    waypointStaticUntil = System.currentTimeMillis() + 2000;
                }
            }
            case RANDOM_MOVE -> {
                cachedX += (int) (randMoveDX * MOVE_SPEED);
                cachedY += (int) (randMoveDY * MOVE_SPEED);
                if (cachedX < 0) { cachedX = 0; randMoveDX = Math.abs(randMoveDX); }
                if (cachedX + imgW > screenW) { cachedX = screenW - imgW; randMoveDX = -Math.abs(randMoveDX); }
                if (cachedY < 0) { cachedY = 0; randMoveDY = Math.abs(randMoveDY); }
                if (cachedY + imgH > screenH) { cachedY = screenH - imgH; randMoveDY = -Math.abs(randMoveDY); }
                syncFractionsFromPixels(screenW, screenH);
            }
            case RANDOM_POSITION -> {
                boolean advance;
                if (animated) {
                    advance = consumeAnimationLoopCount() > 0;
                } else {
                    advance = System.currentTimeMillis() >= waypointStaticUntil;
                }
                if (advance) {
                    int nx = RAND.nextInt(Math.max(1, screenW - imgW));
                    int ny = RAND.nextInt(Math.max(1, screenH - imgH));
                    cachedX = nx;
                    cachedY = ny;
                    waypointStaticUntil = System.currentTimeMillis() + 2000;
                    syncFractionsFromPixels(screenW, screenH);
                }
            }
        }
    }

    private void randomizeMoveDirection() {
        double angle = RAND.nextDouble() * Math.PI * 2;
        randMoveDX = Math.cos(angle);
        randMoveDY = Math.sin(angle);
    }

    public void generateRandomDirection() {
        randomizeMoveDirection();
    }

    @Override
    public void close() {
        for (NativeImageBackedTexture frame : frames) {
            if (frame != null) frame.close();
        }
    }

    public record Waypoint(double xFraction, double yFraction) {}
}
