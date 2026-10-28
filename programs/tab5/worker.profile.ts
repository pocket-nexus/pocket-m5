import { defineProfile } from "@pocketjs/framework/profile";
import base from "./profile";

export default defineProfile({
  ...base,
  id: "m5stack-tab5-worker",
  capabilities: [...base.capabilities, "runtime.worker", "io.i2c", "io.uart"],
});
