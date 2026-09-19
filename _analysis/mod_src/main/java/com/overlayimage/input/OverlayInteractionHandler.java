package com.overlayimage.input;

import com.overlayimage.overlay.ImageOverlay;
import com.overlayimage.overlay.MovementMode;
import com.overlayimage.overlay.OverlayManager;
import net.fabricmc.fabric.api.client.screen.v1.ScreenEvents;
import net.fabricmc.fabric.api.client.screen.v1.ScreenKeyboardEvents;
import net.fabricmc.fabric.api.client.screen.v1.ScreenMouseEvents;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.ChatScreen;
import org.lwjgl.glfw.GLFW;

import java.util.ArrayList;
import java.util.List;

public class OverlayInteractionHandler {
    private static final int CORNER_SIZE = 16;
    private static final double RESIZE_SPEED = 0.1;

    private final OverlayManager mgr;
    private ImageOverlay dragTarget, resizeTarget;
    private ImageOverlay contextMenuTarget;
    private boolean showMenu;
    private int menuX, menuY;
    private final List<String> menuItems = new ArrayList<>();
    private long lastClickTime;
    private ImageOverlay lastClickTarget;

    public OverlayInteractionHandler(OverlayManager mgr) { this.mgr = mgr; }

    public void register() {
        ScreenEvents.BEFORE_INIT.register((client, screen, scaledWidth, scaledHeight) -> {
            if (!(screen instanceof ChatScreen)) return;
            mgr.setInteracting(true);
            mgr.setPaused(true);

            ScreenMouseEvents.allowMouseClick(screen).register((s, mx, my, button) -> {
                if (mgr.isCapturingKey()) return true;

                if (mgr.isPickingWaypoints()) {
                    if (button == 0) {
                        MinecraftClient c = MinecraftClient.getInstance();
                        int sw = c.getWindow().getScaledWidth();
                        int sh = c.getWindow().getScaledHeight();
                        mgr.addWaypoint((double) mx / sw, (double) my / sh);
                        return false;
                    }
                    if (button == 1) { mgr.removeLastWaypoint(); return false; }
                    return false;
                }

                if (showMenu) {
                    int sel = getMenuItemAt((int) mx, (int) my);
                    if (sel >= 0) { executeMenuItem(sel); showMenu = false; contextMenuTarget = null; return false; }
                    showMenu = false; contextMenuTarget = null; return true;
                }

                if (button == 1) {
                    ImageOverlay ov = mgr.getOverlayAt((int) mx, (int) my);
                    if (ov != null) {
                        openContextMenu(ov, (int) mx, (int) my);
                        return false;
                    }
                    return true;
                }
                if (button != 0) return true;

                ImageOverlay ov = mgr.getOverlayAtCorner((int) mx, (int) my, CORNER_SIZE);
                if (ov != null) { ov.startResize(); resizeTarget = ov; mgr.setSelectedOverlay(ov); return false; }
                ov = mgr.getOverlayAt((int) mx, (int) my);
                if (ov != null) {
                    long now = System.currentTimeMillis();
                    if (ov == lastClickTarget && now - lastClickTime < 350) {
                        ov.toggleVisible(); mgr.saveOverlays();
                        lastClickTarget = null;
                        return false;
                    }
                    lastClickTarget = ov; lastClickTime = now;
                    ov.startDrag((int) mx, (int) my); dragTarget = ov; mgr.setSelectedOverlay(ov); return false; }
                mgr.setSelectedOverlay(null);
                return true;
            });

            ScreenMouseEvents.afterMouseRelease(screen).register((s, mx, my, button) -> {
                if (dragTarget != null) { dragTarget.stopDrag(); dragTarget = null; }
                if (resizeTarget != null) { resizeTarget.stopDrag(); resizeTarget = null; }
                syncAndSave();
            });

            ScreenMouseEvents.allowMouseScroll(screen).register((s, mx, my, h, v) -> {
                if (mgr.isPickingWaypoints() || mgr.isCapturingKey() || showMenu) return false;
                ImageOverlay ov = mgr.getSelectedOverlay();
                if (ov != null && ov.containsPoint((int) mx, (int) my)) {
                    ov.setScale(ov.getScale() + (float) (v * RESIZE_SPEED));
                    syncAndSave();
                    return false;
                }
                return true;
            });

            ScreenEvents.afterRender(screen).register((s, dc, mx, my, td) -> {
                if (dragTarget != null && dragTarget.isDragging()) dragTarget.dragTo((int) mx, (int) my);
                if (resizeTarget != null && resizeTarget.isResizing()) resizeTarget.resizeTo((int) mx);
                if (showMenu) renderContextMenu(dc, (int) mx, (int) my);
            });

            ScreenKeyboardEvents.allowKeyPress(screen).register((s, key, scancode, modifiers) -> {
                if (mgr.isPickingWaypoints()) {
                    if (key == GLFW.GLFW_KEY_ENTER) {
                        MinecraftClient c = MinecraftClient.getInstance();
                        mgr.confirmWaypoints(c.getWindow().getScaledWidth(), c.getWindow().getScaledHeight());
                        return false;
                    }
                    if (key == GLFW.GLFW_KEY_ESCAPE) { mgr.cancelWaypoints(); return false; }
                    return false;
                }
                if (mgr.isCapturingKey()) {
                    if (key == GLFW.GLFW_KEY_ESCAPE) { mgr.stopKeyCapture(); return false; }
                    if (key == GLFW.GLFW_KEY_BACKSPACE) { mgr.setCapturedKey(GLFW.GLFW_KEY_UNKNOWN); mgr.stopKeyCapture(); return false; }
                    return false;
                }
                return true;
            });

            ScreenEvents.remove(screen).register(s -> {
                mgr.setInteracting(false); mgr.setPaused(false);
                mgr.setSelectedOverlay(null);
                if (dragTarget != null) { dragTarget.stopDrag(); dragTarget = null; }
                if (resizeTarget != null) { resizeTarget.stopDrag(); resizeTarget = null; }
                showMenu = false; contextMenuTarget = null;
                if (mgr.isPickingWaypoints()) mgr.cancelWaypoints();
                if (mgr.isCapturingKey()) mgr.stopKeyCapture();
            });
        });
    }

    private void openContextMenu(ImageOverlay overlay, int mx, int my) {
        contextMenuTarget = overlay; showMenu = true; menuX = mx; menuY = my;
        mgr.setSelectedOverlay(overlay);
        menuItems.clear();
        menuItems.add("定点移动");
        menuItems.add("随机运动");
        menuItems.add("随机展示");
        menuItems.add("重置运动");
        menuItems.add("按键绑定");
        menuItems.add("删除");
    }

    private int getMenuItemAt(int mx, int my) {
        if (!showMenu) return -1;
        int itemH = 14;
        for (int i = 0; i < menuItems.size(); i++) {
            int y = menuY + i * itemH;
            if (mx >= menuX && mx <= menuX + 100 && my >= y && my <= y + itemH) return i;
        }
        return -1;
    }

    private void executeMenuItem(int index) {
        if (contextMenuTarget == null) return;
        switch (index) {
            case 0 -> { showMenu = false; mgr.startWaypointPicking(contextMenuTarget); }
            case 1 -> { contextMenuTarget.setMovementMode(MovementMode.RANDOM_MOVE); contextMenuTarget.generateRandomDirection(); mgr.saveOverlays(); }
            case 2 -> { contextMenuTarget.setMovementMode(MovementMode.RANDOM_POSITION); mgr.saveOverlays(); }
            case 3 -> { contextMenuTarget.setMovementMode(MovementMode.NONE); mgr.saveOverlays(); }
            case 4 -> { showMenu = false; mgr.startKeyCapture(contextMenuTarget); }
            case 5 -> mgr.removeOverlay(contextMenuTarget);
        }
    }

    private void renderContextMenu(DrawContext dc, int mx, int my) {
        int itemH = 14, w = 100, h = menuItems.size() * itemH;
        int bg = 0xCC000000, border = 0xFFAAAAAA;
        dc.fill(menuX, menuY, menuX + w, menuY + h, bg);
        dc.fill(menuX - 1, menuY - 1, menuX + w + 1, menuY, border);
        dc.fill(menuX - 1, menuY + h, menuX + w + 1, menuY + h + 1, border);
        dc.fill(menuX - 1, menuY, menuX, menuY + h, border);
        dc.fill(menuX + w, menuY, menuX + w + 1, menuY + h, border);
        for (int i = 0; i < menuItems.size(); i++) {
            int y = menuY + i * itemH;
            boolean hover = mx >= menuX && mx <= menuX + w && my >= y && my <= y + itemH;
            if (hover) dc.fill(menuX + 1, y, menuX + w - 1, y + itemH, 0x4488CCFF);
            dc.drawText(MinecraftClient.getInstance().textRenderer, menuItems.get(i), menuX + 4, y + 3, 0xFFFFFFFF, true);
        }
    }

    public boolean isShowingMenu() { return showMenu; }

    private void syncAndSave() {
        MinecraftClient c = MinecraftClient.getInstance();
        if (c.getWindow() != null) mgr.syncFractions(c.getWindow().getScaledWidth(), c.getWindow().getScaledHeight());
        mgr.flushPendingSave();
    }
}
