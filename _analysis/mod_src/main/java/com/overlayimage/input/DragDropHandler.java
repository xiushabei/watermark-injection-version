package com.overlayimage.input;

import com.overlayimage.OverlayImageMod;
import com.overlayimage.gif.GifDecoder;
import com.overlayimage.overlay.ImageOverlay;
import com.overlayimage.overlay.OverlayManager;
import net.minecraft.client.MinecraftClient;
import org.lwjgl.glfw.GLFWDropCallbackI;
import org.lwjgl.system.MemoryUtil;
import org.lwjgl.system.Pointer;

import java.io.IOException;

public class DragDropHandler implements GLFWDropCallbackI {
    private final OverlayManager overlayManager;
    private final MinecraftClient client;

    public DragDropHandler(OverlayManager overlayManager, MinecraftClient client) {
        this.overlayManager = overlayManager;
        this.client = client;
    }

    @Override
    public void invoke(long window, int count, long names) {
        for (int i = 0; i < count; i++) {
            long namePtr = MemoryUtil.memGetAddress(names + (long) i * Pointer.POINTER_SIZE);
            String filePath = MemoryUtil.memUTF8(namePtr);
            if (filePath == null) continue;

            String lower = filePath.toLowerCase();
            if (!lower.endsWith(".png") && !lower.endsWith(".jpg") &&
                !lower.endsWith(".jpeg") && !lower.endsWith(".gif")) continue;

            final String path = filePath;
            final String ext = lower.substring(lower.lastIndexOf('.'));

            client.execute(() -> {
                try {
                    String backupName = overlayManager.copyImageToBackup(path);
                    String backupPath = overlayManager.getConfigDir()
                            .resolve("images").resolve(backupName).toString();

                    GifDecoder.LoadResult result = GifDecoder.loadImage(backupPath);
                    ImageOverlay overlay = new ImageOverlay(
                            backupName,
                            new java.io.File(path).getName(),
                            result.frames(),
                            result.delays(),
                            result.width(),
                            result.height()
                    );
                    overlayManager.addOverlay(overlay);
                    OverlayImageMod.LOGGER.info("Loaded image: {}", backupName);
                } catch (Exception e) {
                    OverlayImageMod.LOGGER.error("Failed to load image: {}", path, e);
                }
            });
        }
    }
}
