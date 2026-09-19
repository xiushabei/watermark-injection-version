package com.overlayimage.gif;

import net.minecraft.client.texture.NativeImage;
import com.overlayimage.OverlayImageMod;
import net.minecraft.client.texture.NativeImageBackedTexture;

import javax.imageio.ImageIO;
import javax.imageio.ImageReader;
import javax.imageio.metadata.IIOMetadata;
import javax.imageio.metadata.IIOMetadataNode;
import javax.imageio.stream.ImageInputStream;
import java.awt.*;
import java.awt.image.BufferedImage;
import java.io.*;
import java.util.ArrayList;
import java.util.List;

public class GifDecoder {

    public static LoadResult loadImage(String filePath) throws Exception {
        String lower = filePath.toLowerCase();
        if (lower.endsWith(".gif")) {
            return loadGif(filePath);
        } else {
            return loadStatic(filePath);
        }
    }

    private static LoadResult loadStatic(String filePath) throws Exception {
        BufferedImage buffered;
        try (FileInputStream fis = new FileInputStream(filePath)) {
            buffered = ImageIO.read(fis);
        }
        if (buffered == null) {
            throw new IOException("Failed to read image: " + filePath);
        }

        NativeImage nativeImage = bufferedImageToNativeImage(buffered);
        NativeImageBackedTexture texture = new NativeImageBackedTexture(nativeImage);

        List<NativeImageBackedTexture> frames = new ArrayList<>();
        frames.add(texture);
        List<Integer> delays = new ArrayList<>();
        delays.add(0);

        return new LoadResult(frames, delays, buffered.getWidth(), buffered.getHeight());
    }

    private static LoadResult loadGif(String filePath) throws Exception {
        ImageReader reader = ImageIO.getImageReadersByFormatName("gif").next();
        List<NativeImageBackedTexture> textures = new ArrayList<>();
        List<Integer> delays = new ArrayList<>();
        int canvasW = 0, canvasH = 0;

        try (ImageInputStream iis = ImageIO.createImageInputStream(new FileInputStream(filePath))) {
            reader.setInput(iis);
            int numFrames = reader.getNumImages(true);

            for (int i = 0; i < numFrames; i++) {
                BufferedImage raw = reader.read(i);
                IIOMetadata meta = reader.getImageMetadata(i);

                if (i == 0) {
                    canvasW = raw.getWidth();
                    canvasH = raw.getHeight();
                }

                // 一次解析所有帧元数据
                GifFrameMetadata frameMeta = parseFrameMetadata(meta);

                BufferedImage frame = new BufferedImage(canvasW, canvasH, BufferedImage.TYPE_INT_ARGB);
                Graphics2D g = frame.createGraphics();
                g.drawImage(raw, frameMeta.left(), frameMeta.top(), null);
                g.dispose();

                NativeImage ni = bufferedImageToNativeImage(frame);
                textures.add(new NativeImageBackedTexture(ni));
                delays.add(frameMeta.delay());

                OverlayImageMod.LOGGER.debug("GIF frame {}: disposal={} delay={}",
                        i, frameMeta.disposal(), frameMeta.delay());
            }
        } finally {
            reader.dispose();
        }

        if (textures.isEmpty()) throw new IOException("GIF has no frames: " + filePath);
        return new LoadResult(textures, delays, canvasW, canvasH);
    }

    private static NativeImage bufferedImageToNativeImage(BufferedImage image) {
        int w = image.getWidth();
        int h = image.getHeight();
        NativeImage ni = new NativeImage(NativeImage.Format.RGBA, w, h, false);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                ni.setColorArgb(x, y, image.getRGB(x, y));
            }
        }
        return ni;
    }

    private record GifFrameMetadata(int left, int top, int delay, String disposal) {}

    private static GifFrameMetadata parseFrameMetadata(IIOMetadata meta) {
        try {
            IIOMetadataNode root = (IIOMetadataNode) meta.getAsTree(meta.getNativeMetadataFormatName());
            int left = 0, top = 0, delay = 100;
            String disposal = "none";

            IIOMetadataNode desc = (IIOMetadataNode) root.getElementsByTagName("ImageDescriptor").item(0);
            if (desc != null) {
                String lp = desc.getAttribute("imageLeftPosition");
                if (lp != null && !lp.isEmpty()) left = Integer.parseInt(lp);
                String tp = desc.getAttribute("imageTopPosition");
                if (tp != null && !tp.isEmpty()) top = Integer.parseInt(tp);
            }

            IIOMetadataNode gce = (IIOMetadataNode) root.getElementsByTagName("GraphicControlExtension").item(0);
            if (gce != null) {
                String dt = gce.getAttribute("delayTime");
                if (dt != null && !dt.isEmpty()) delay = Math.max(Integer.parseInt(dt) * 10, 20);
                String dm = gce.getAttribute("disposalMethod");
                if (dm != null && !dm.isEmpty()) disposal = dm;
            }

            return new GifFrameMetadata(left, top, delay, disposal);
        } catch (Exception e) {
            OverlayImageMod.LOGGER.warn("Failed to parse GIF frame metadata", e);
            return new GifFrameMetadata(0, 0, 100, "none");
        }
    }

    public record LoadResult(List<NativeImageBackedTexture> frames, List<Integer> delays,
                             int width, int height) {}
}
