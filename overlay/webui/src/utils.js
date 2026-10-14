import { toast } from 'kernelsu';
import { susfs_bin } from './core/constants.js';

export async function run(command) {
    return new Promise((resolve, reject) => {
        const callbackName = `exec_callback_${Date.now()}`;
        window[callbackName] = (errno, stdout, stderr) => {
            delete window[callbackName];
            if (errno === 0) {
                resolve(stdout);
            } else {
                console.error(`Error executing command: ${stderr}`);
                reject(stderr);
            }
        };
        try {
            ksu.exec(command, "{}", callbackName);
        } catch (error) {
            console.error(`Execution error: ${error}`);
            reject(error);
        }
    });
}

export function posixQuote(value) {
    return `'${String(value).replace(/'/g, "'\\''")}'`;
}

export async function ctl(request) {
    const payload = JSON.stringify(request);
    return JSON.parse(await run(`${susfs_bin} --json ${posixQuote(payload)}`));
}

export function catToObject(content) {
    return content
        .split('\n')
        .filter(line => line.includes('='))
        .reduce((acc, line) => {
            const [key, ...rest] = line.split('=');
            const value = rest.join('=').trim();
            const trimmedKey = key.trim();

            if ((value.startsWith("'") && value.endsWith("'")) ||
                (value.startsWith('"') && value.endsWith('"'))) {
                acc[trimmedKey] = value.slice(1, -1);
            } else {
                acc[trimmedKey] = isNaN(Number(value)) ? value : Number(value);
            }

            return acc;
        }, {});
}

export function parseVersion(versionStr) {
    const parts = versionStr.split('-')[0].replace(/^v/, '').split('.');
    return {
        main: parseInt(parts[0]) || 0,
        sub: parseInt(parts[1]) || 0,
        patch: parseInt(parts[2]) || 0,
    };
}

export function versionAtLeast(v, major, minor, patch) {
    if (v.main !== major) return v.main > major;
    if (v.sub !== minor) return v.sub > minor;
    return v.patch >= patch;
}

export async function updateConfig(configPath, key, value) {
    await ctl({ operation: "config.set", values: { key, value: String(value) } });
}

export function setupBooleanToggle(element, settings, key, configPath, {
    onMessage = "Reboot to take effect",
    offMessage = "Reboot to take effect",
    onAction = null,
    offAction = null,
    onstate = 1,
    offstate = 0,
} = {}) {
    element.addEventListener("click", async function () {
        if (settings[key] >= onstate) {
            await updateConfig(configPath, key, offstate);
            settings[key] = offstate;
            if (offAction) await offAction();
            toast(offMessage);
        } else {
            await updateConfig(configPath, key, onstate);
            settings[key] = onstate;
            if (onAction) await onAction();
            toast(onMessage);
        }
    });
}

export function escapeHtml(text) {
    const map = {
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#039;'
    };
    return text.replace(/[&<>"']/g, m => map[m]);
}
