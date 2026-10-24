// M5Stack Tab5: 720x1280 MIPI-DSI panel in its native portrait orientation,
// presented at raster density 2. Touch contacts arrive in logical pixels.
import { defineProfile } from "@pocketjs/framework/profile";

export default defineProfile({
  id: "m5stack-tab5",
  platform: "esp-idf",
  form: "takeover",
  tickHz: 60,
  display: {
    physicalViewport: [720, 1280],
    logicalViewports: [[360, 640]],
    presentations: ["native"],
    rasterDensity: 2,
  },
  capabilities: ["input.touch", "text.glyphs.baked"],
});
