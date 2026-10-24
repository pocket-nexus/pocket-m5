// Home: the launcher embedded in the firmware. It lists the packages the host
// admitted from the SD card and launches them through appLaunch.
import { For, Show } from "solid-js";
import { Text, View } from "@pocketjs/framework/solid/components";
import { getOps } from "@pocketjs/framework/host";
import { launchApp } from "@pocketjs/framework/launcher";

interface InstalledApp {
  output: string;
  id: string;
  title: string;
  bytes?: number;
}

/** appTable as the pocket_app_host component reports it. */
interface HomeTable {
  apps: InstalledApp[];
  current: string;
  resume: string | null;
  launcher?: string;
  storage?: "ready" | "missing";
  lastError?: { output: string; message: string } | null;
  installErrors?: { file: string; message: string }[];
}

function readTable(): HomeTable {
  const raw = getOps().appTable?.();
  if (!raw) return { apps: [], current: "", resume: null, storage: "missing" };
  const table = JSON.parse(raw) as HomeTable;
  return { ...table, apps: table.apps ?? [] };
}

function size(bytes: number | undefined): string {
  if (bytes === undefined) return "";
  return bytes >= 1048576 ? `${(bytes / 1048576).toFixed(1)} MB` : `${Math.ceil(bytes / 1024)} KB`;
}

function AppTile(props: { app: InstalledApp; resume: boolean }) {
  return (
    <View
      focusable
      onPress={() => launchApp(props.app.output)}
      class="w-[154] h-[96] p-3 flex-col justify-between rounded-xl bg-slate-800 active:bg-slate-700"
    >
      <Text class="text-base text-white font-bold">{props.app.title}</Text>
      <View class="flex-row items-center justify-between">
        <Text class="text-xs text-slate-400">{size(props.app.bytes)}</Text>
        <Show when={props.resume}>
          <Text class="text-xs text-emerald-400 font-bold">RESUME</Text>
        </Show>
      </View>
    </View>
  );
}

export default function Home() {
  const table = readTable();
  const errors = table.installErrors ?? [];
  return (
    <View class="w-full h-full flex-col p-5 gap-4 bg-slate-950">
      <View class="flex-col gap-1">
        <Text class="text-2xl text-white font-bold">PocketJS</Text>
        <Text class="text-sm text-slate-400">M5Stack Tab5</Text>
      </View>

      <Show when={table.lastError}>
        <View class="p-3 flex-col gap-1 rounded-lg bg-red-950">
          <Text class="text-sm text-red-300 font-bold">{table.lastError?.output} stopped</Text>
          <Text class="text-xs text-red-200">{table.lastError?.message}</Text>
        </View>
      </Show>

      <Show
        when={table.apps.length > 0}
        fallback={
          <View class="p-4 flex-col gap-2 rounded-xl bg-slate-900">
            <Text class="text-base text-white font-bold">
              {table.storage === "missing" ? "No SD card" : "No apps installed"}
            </Text>
            <Text class="text-sm text-slate-400">
              Copy .pocket packages built for m5stack-tab5 to /pocket/inbox on the SD card, then restart.
            </Text>
          </View>
        }
      >
        <View class="flex-row flex-wrap gap-3">
          <For each={table.apps}>{(app) => <AppTile app={app} resume={app.output === table.resume} />}</For>
        </View>
      </Show>

      <For each={errors}>
        {(error) => (
          <View class="p-3 flex-col gap-1 rounded-lg bg-amber-950">
            <Text class="text-sm text-amber-300 font-bold">{error.file} rejected</Text>
            <Text class="text-xs text-amber-200">{error.message}</Text>
          </View>
        )}
      </For>

      <View class="flex-1" />
      <Text class="text-xs text-slate-500">Swipe up from the bottom edge to return here.</Text>
    </View>
  );
}
