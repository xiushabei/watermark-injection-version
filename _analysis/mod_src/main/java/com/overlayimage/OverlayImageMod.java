package com.overlayimage;

import com.overlayimage.input.DragDropHandler;
import com.overlayimage.input.OverlayInteractionHandler;
import com.overlayimage.overlay.OverlayManager;
import com.overlayimage.render.OverlayRenderer;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.rendering.v1.HudRenderCallback;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.util.Window;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.glfw.GLFWDropCallbackI;
import org.lwjgl.glfw.GLFWKeyCallbackI;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayDeque;

public class OverlayImageMod implements ClientModInitializer {
    public static final String MOD_ID = "watermark";
    public static final Logger LOGGER = LoggerFactory.getLogger(MOD_ID);

    private static OverlayManager mgr;
    private static GLFWDropCallbackI dropCallback;
    private static boolean dropRegistered, savedLoaded, keyCallbackRegistered;
    private static GLFWKeyCallbackI prevKeyCallback;
    private static final ArrayDeque<Integer> keyPressQueue = new ArrayDeque<>();

    @Override
    public void onInitializeClient() {
        LOGGER.info("Watermark v1 initializing (Fabric 1.21.4)...");

        mgr = new OverlayManager();
        OverlayRenderer renderer = new OverlayRenderer(mgr);
        OverlayInteractionHandler iHandler = new OverlayInteractionHandler(mgr);

        HudRenderCallback.EVENT.register(renderer);
        dropCallback = new DragDropHandler(mgr, MinecraftClient.getInstance());

        ClientTickEvents.START_CLIENT_TICK.register(client -> {
            if (client.getWindow() == null) return;

            long h = client.getWindow().getHandle();
            if (!savedLoaded) { mgr.loadSavedOverlays(); savedLoaded = true; }
            if (!dropRegistered) {
                GLFW.glfwSetDropCallback(h, dropCallback);
                dropRegistered = true;
            }
            if (!keyCallbackRegistered) {
                prevKeyCallback = GLFW.glfwSetKeyCallback(h, (window, key, scancode, action, mods) -> {
                    if (action == GLFW.GLFW_PRESS && key != GLFW.GLFW_KEY_UNKNOWN) {
                        keyPressQueue.addLast(key);
                    }
                    if (prevKeyCallback != null) {
                        prevKeyCallback.invoke(window, key, scancode, action, mods);
                    }
                });
                keyCallbackRegistered = true;
            }

            // Process all queued key presses (event-driven, no polling)
            while (!keyPressQueue.isEmpty()) {
                int key = keyPressQueue.removeFirst();
                if (mgr.isCapturingKey()) {
                    mgr.setCapturedKey(key);
                    mgr.stopKeyCapture();
                } else if (client.currentScreen == null) {
                    for (var ov : mgr.getOverlays()) {
                        if (ov.getBoundKeyCode() == key) {
                            ov.toggleVisible();
                        }
                    }
                }
            }

            Window win = client.getWindow();
            if (!mgr.isPaused()) mgr.tickMovement(win.getScaledWidth(), win.getScaledHeight());
        });

        iHandler.register();
        LOGGER.info("Watermark v1 initialized!");
    }

    public static OverlayManager getOverlayManager() { return mgr; }
}
