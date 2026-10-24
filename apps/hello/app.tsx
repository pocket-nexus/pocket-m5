// Hello: an installable sample. Build it for the m5stack-tab5 profile with
// `bun run app build apps/hello/pocket.json` and copy the package to the SD card.
import { createSignal } from "solid-js";
import { Text, View } from "@pocketjs/framework/solid/components";

export default function Hello() {
  const [count, setCount] = createSignal(0);
  return (
    <View class="w-full h-full flex-col items-center justify-center gap-6 bg-slate-50">
      <Text class="text-3xl text-slate-950 font-bold">Hello, Tab5</Text>
      <View
        focusable
        onPress={() => setCount(count() + 1)}
        class="px-6 py-3 rounded-xl bg-blue-600 active:bg-blue-700"
      >
        <Text class="text-xl text-white font-bold">Tap: {count()}</Text>
      </View>
      <Text class="text-sm text-slate-500">Swipe up from the bottom edge to go home.</Text>
    </View>
  );
}
