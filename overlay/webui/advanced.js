import { ctl } from './src/utils.js';

const api = {
    refresh: { operation: "status" },
    log_on: { operation: "log.set", values: { enabled: true } },
    log_off: { operation: "log.set", values: { enabled: false } },
    avc_on: { operation: "avc.set", values: { enabled: true } },
    avc_off: { operation: "avc.set", values: { enabled: false } },
    path_clear: { operation: "path.clear" },
    kstat_clear: { operation: "kstat.clear" },
    redirect_clear: { operation: "redirect.clear" },
    modules_clear: { operation: "modules.clear" },
    mounts_clear: { operation: "mounts.clear" },
    mounts_reset: { operation: "mounts.reset" }
};

const status = document.getElementById("status");
const output = document.getElementById("output");

function show(value) {
    output.textContent = typeof value === "string" ? value : JSON.stringify(value, null, 2);
}

async function invoke(request) {
    try {
        const result = await ctl(request);
        show(result);
        await refresh();
    } catch (error) {
        show(String(error));
    }
}

async function refresh() {
    try {
        const result = await ctl({ operation: "status" });
        const data = result.data || {};
        status.textContent = `${result.backend} ${result.version}\n${JSON.stringify(data.diagnostics || {}, null, 2)}`;
    } catch (error) {
        status.textContent = String(error);
    }
}

for (const [id, request] of Object.entries(api)) {
    document.getElementById(id).addEventListener("click", () => invoke(request));
}

document.getElementById("path_delete").addEventListener("click", () => {
    const path = document.getElementById("path_input").value;
    if (path) invoke({ operation: "path.delete", values: { path } });
});
document.getElementById("kstat_delete").addEventListener("click", () => {
    const path = document.getElementById("path_input").value;
    if (path) invoke({ operation: "kstat.delete", values: { path } });
});
document.getElementById("kstat_full").addEventListener("click", () => {
    const path = document.getElementById("path_input").value;
    if (path) invoke({ operation: "kstat.full_clone", values: { path } });
});
document.getElementById("redirect_delete").addEventListener("click", () => {
    const path = document.getElementById("path_input").value;
    if (path) invoke({ operation: "redirect.delete", values: { path } });
});
document.getElementById("module_add").addEventListener("click", () => {
    const name = document.getElementById("module_input").value;
    if (name) invoke({ operation: "modules.add", values: { name } });
});
document.getElementById("module_del").addEventListener("click", () => {
    const name = document.getElementById("module_input").value;
    if (name) invoke({ operation: "modules.del", values: { name } });
});
document.getElementById("mount_add").addEventListener("click", () => {
    const prefix = document.getElementById("mount_input").value;
    if (prefix) invoke({ operation: "mounts.add", values: { prefix } });
});
document.getElementById("mount_del").addEventListener("click", () => {
    const prefix = document.getElementById("mount_input").value;
    if (prefix) invoke({ operation: "mounts.del", values: { prefix } });
});
document.getElementById("rule_load").addEventListener("click", async () => {
    try {
        const rule = document.getElementById("rule_name").value;
        const result = await ctl({ operation: "rules.get", values: { rule } });
        document.getElementById("rule_content").value = result.data.content || "";
    } catch (error) {
        show(String(error));
    }
});
document.getElementById("rule_save").addEventListener("click", () => {
    const rule = document.getElementById("rule_name").value;
    const content = document.getElementById("rule_content").value;
    invoke({ operation: "rules.replace", values: { rule, content } });
});

refresh();
