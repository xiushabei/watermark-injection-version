package com.overlayimage.render;

import com.mojang.blaze3d.systems.RenderSystem;
import com.overlayimage.overlay.ImageOverlay;
import com.overlayimage.overlay.MovementMode;
import com.overlayimage.overlay.OverlayManager;
import net.fabricmc.fabric.api.client.rendering.v1.HudRenderCallback;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.font.TextRenderer;
import net.minecraft.client.gl.ShaderProgramKeys;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.render.*;
import net.minecraft.client.texture.NativeImageBackedTexture;
import net.minecraft.client.util.Window;
import net.minecraft.text.Text;
import net.minecraft.util.Identifier;
import org.joml.Matrix4f;

public class OverlayRenderer implements HudRenderCallback {
    private final OverlayManager mgr;

    public OverlayRenderer(OverlayManager mgr) { this.mgr = mgr; }

    @Override
    public void onHudRender(DrawContext dc, RenderTickCounter tc) {
        MinecraftClient client = MinecraftClient.getInstance();
        if (client.options.hudHidden) return;
        Window win = client.getWindow();
        int sw = win.getScaledWidth();
        int sh = win.getScaledHeight();

        if (mgr.isPickingWaypoints()) {
            renderWaypointPicker(dc, sw, sh);
            return;
        }
        if (mgr.isCapturingKey()) {
            renderKeyCaptureOverlay(dc, sw, sh);
            return;
        }

        if (!mgr.isPaused()) mgr.tickMovement(sw, sh);

        RenderSystem.enableBlend();
        RenderSystem.defaultBlendFunc();
        for (ImageOverlay ov : mgr.getOverlays()) {
            boolean isHidden = !ov.isVisible();
            if (isHidden && !mgr.isInteracting()) continue;
            ov.updateFromScreenSize(sw, sh);
            registerTexturesIfNeeded(client, ov);

            int fi = ov.getCurrentFrameIndex();
            Identifier tid = getTextureId(ov, fi);
            if (isHidden) {
                RenderSystem.setShaderColor(1f, 1f, 1f, 0.3f);
            }
            renderTexture(dc, tid, ov.getX(), ov.getY(), ov.getDisplayWidth(), ov.getDisplayHeight());
            if (isHidden) {
                RenderSystem.setShaderColor(1f, 1f, 1f, 1f);
            }

            if (client.currentScreen != null && ov == mgr.getSelectedOverlay()) {
                renderSelectionBorder(dc, ov);
            }
        }
        RenderSystem.disableBlend();
    }

    private void renderWaypointPicker(DrawContext dc, int sw, int sh) {
        dc.fill(0, 0, sw, sh, 0x88000000);
        TextRenderer tr = MinecraftClient.getInstance().textRenderer;
        String hint = "左键添加标点(最多7个) | 右键撤销 | Enter确认 | Esc取消";
        dc.drawText(tr, Text.literal(hint), 10, 10, 0xFFFFFF00, true);

        var wps = mgr.getPendingWaypoints();
        for (int i = 0; i < wps.size(); i++) {
            var wp = wps.get(i);
            int px = (int) (wp.xFraction() * sw);
            int py = (int) (wp.yFraction() * sh);
            int s = 12;
            dc.fill(px - s, py - s, px + s, py - s + 1, 0xFFFF3333);
            dc.fill(px - s, py + s, px + s, py + s + 1, 0xFFFF3333);
            dc.fill(px - s, py - s, px - s + 1, py + s, 0xFFFF3333);
            dc.fill(px + s, py - s, px + s + 1, py + s, 0xFFFF3333);
            dc.drawText(tr, Text.literal(String.valueOf(i + 1)), px - 3, py - 4, 0xFFFF3333, false);
        }
    }

    private void renderKeyCaptureOverlay(DrawContext dc, int sw, int sh) {
        dc.fill(0, 0, sw, sh, 0xAA000000);
        TextRenderer tr = MinecraftClient.getInstance().textRenderer;
        String msg = "请按下按键以绑定... (Esc取消)";
        int tw = tr.getWidth(msg);
        dc.drawText(tr, Text.literal(msg), (sw - tw) / 2, sh / 2 - 10, 0xFFFFFFFF, true);
    }

    private void registerTexturesIfNeeded(MinecraftClient client, ImageOverlay ov) {
        if (!ov.needsTextureRegistration()) return;
        for (int i = 0; i < ov.getTotalFrames(); i++) {
            NativeImageBackedTexture tex = ov.getFrame(i);
            Identifier id = getTextureId(ov, i);
            client.getTextureManager().registerTexture(id, tex);
        }
        ov.markTexturesRegistered();
    }

    private static Identifier getTextureId(ImageOverlay ov, int fi) {
        return Identifier.of("watermark", "overlay_" + ov.getBackupFileName() + "_" + fi);
    }

    private void renderTexture(DrawContext dc, Identifier tid, int x, int y, int w, int h) {
        RenderSystem.setShader(ShaderProgramKeys.POSITION_TEX);
        RenderSystem.setShaderTexture(0, tid);
        Matrix4f matrix = dc.getMatrices().peek().getPositionMatrix();
        Tessellator t = Tessellator.getInstance();
        BufferBuilder b = t.begin(VertexFormat.DrawMode.QUADS, VertexFormats.POSITION_TEXTURE);
        b.vertex(matrix, (float) x, (float) y, 0).texture(0f, 0f);
        b.vertex(matrix, (float) x, (float) y + h, 0).texture(0f, 1f);
        b.vertex(matrix, (float) x + w, (float) y + h, 0).texture(1f, 1f);
        b.vertex(matrix, (float) x + w, (float) y, 0).texture(1f, 0f);
        BufferRenderer.drawWithGlobalProgram(b.end());
    }

    private void renderSelectionBorder(DrawContext dc, ImageOverlay ov) {
        int x1 = ov.getX() - 1, y1 = ov.getY() - 1;
        int x2 = ov.getX() + ov.getDisplayWidth() + 1;
        int y2 = ov.getY() + ov.getDisplayHeight() + 1;
        int bc = 0xFFFFFF00;
        dc.fill(x1, y1, x2, y1 + 1, bc);
        dc.fill(x1, y2 - 1, x2, y2, bc);
        dc.fill(x1, y1, x1 + 1, y2, bc);
        dc.fill(x2 - 1, y1, x2, y2, bc);
        int hs = 8;
        dc.fill(ov.getX() + ov.getDisplayWidth() - hs / 2, ov.getY() + ov.getDisplayHeight() - hs / 2,
                ov.getX() + ov.getDisplayWidth() + hs / 2, ov.getY() + ov.getDisplayHeight() + hs / 2, 0xFF00FF00);
    }
}
