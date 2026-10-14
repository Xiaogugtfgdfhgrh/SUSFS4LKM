import { FileSelector } from '../file_selector/file_selector.js';
import { toast } from 'kernelsu';
import { run, catToObject, parseVersion, versionAtLeast, posixQuote } from '../../utils.js';
import { config } from '../../core/constants.js';
import { sus_su_toggle } from './sus-su.js';
import { set_uname } from './uname.js';
import { susfs_log_toggle } from './log.js';
import { auto_hide_settings } from '../auto-hide/auto-hide.js';
import { export_susfs_config } from './export-logs.js';

export async function susfs_import_config(susfs_versions, susfs_features) {
    const fileSelector = new FileSelector();
    fileSelector.appendTo(document.body);
    const importButton = document.getElementById('susfs_import');
    const confirm_import_modal = document.getElementById('confirm_import_modal');
    const import_modal_cancel = document.getElementById('import_modal_cancel');
    const import_modal_confirm = document.getElementById('import_modal_confirm');
    const import_modal_backup = document.getElementById('import_modal_backup');
    importButton.addEventListener('click', async () => {
        fileSelector.getFilePath('tar.gz').then(async (filePath) => {
            if (!filePath) {
                toast('No file selected.');
                return;
            }
            const LegitSusfsConfig = await run(`tar -tzf ${posixQuote(filePath)}`);
            console.log(LegitSusfsConfig);
            const members = LegitSusfsConfig
                .split(/\r?\n/)
                .map((line) => line.trim().replace(/^\.\//, ''))
                .filter(Boolean);
            const allowedMembers = new Set([
                'config.sh', 'sus_path.txt', 'sus_path_loop.txt', 'sus_maps.txt',
                'sus_mount.txt', 'try_umount.txt', 'sus_open_redirect.txt',
                'legit_mounts.txt', 'sus_kstat_statically.json',
                'susfs_no_auto_add_sus_ksu_default_mount',
                'susfs_no_auto_add_sus_bind_mount',
                'susfs_no_auto_add_try_umount_for_bind_mount',
                'susfs_umount_for_zygote_system_process',
            ]);
            if (!members.includes('config.sh') || members.some((name) => !allowedMembers.has(name)) || new Set(members).size !== members.length) {
                toast('Selected file is not a valid SUSFS config backup.');
                return;
            }
            const archiveDetails = await run(`tar -tvzf ${posixQuote(filePath)}`);
            const unsafeEntry = archiveDetails.split(/\r?\n/).filter(Boolean).some((line) => {
                const fields = line.trim().split(/\s+/);
                const mode = fields[0] || '';
                const name = (fields[fields.length - 1] || '').replace(/^\.\//, '');
                if (name === '' || name === '.') return mode[0] !== 'd';
                return mode[0] !== '-';
            });
            if (unsafeEntry) {
                toast('Selected archive contains non-regular entries.');
                return;
            }
            const importedConfig = await run(`tar -xOzf ${posixQuote(filePath)} config.sh`);
            if (!importedConfig.trim()) {
                toast('Selected config is empty.');
                return;
            }
            const safeConfig = importedConfig.split(/\r?\n/).every((line) => {
                const value = line.trim();
                if (!value || value.startsWith('#')) return true;
                return /^[A-Za-z_][A-Za-z0-9_]*=(?:'[^'\\$`]*'|"[^"\\$`]*"|[A-Za-z0-9_./:-]+)$/.test(value);
            });
            if (!safeConfig) {
                toast('Selected config contains unsupported values.');
                return;
            }
            confirm_import_modal.showModal();
            import_modal_cancel.addEventListener('click', () => {
                confirm_import_modal.close();
            });
            import_modal_confirm.addEventListener('click', async () => {
                try {
                    if (versionAtLeast(susfs_versions, 1, 5, 3) && !versionAtLeast(susfs_versions, 2, 0, 0)) {
                        await run(`tar -xzf ${posixQuote(filePath)} -C ${config}`);
                    }
                    else {
                        await run(`tar --exclude="./susfs_no_auto_add_sus_ksu_default_mount" --exclude="./susfs_no_auto_add_sus_bind_mount" --exclude="./susfs_no_auto_add_try_umount_for_bind_mount" --exclude="./susfs_umount_for_zygote_system_process" -xzf ${posixQuote(filePath)} -C ${config}`);
                    }
                    if (versionAtLeast(susfs_versions, 1, 5, 3) && !versionAtLeast(susfs_versions, 2, 0, 0)) {
                        if (LegitSusfsConfig.includes('susfs_no_auto_add_sus_ksu_default_mount')) {
                            await run(`mv ${config}/susfs_no_auto_add_sus_ksu_default_mount /data/adb/ `);
                        }
                        else {
                            await run(`rm -f /data/adb/susfs_no_auto_add_sus_ksu_default_mount`);
                        }
                        if (LegitSusfsConfig.includes('susfs_no_auto_add_sus_bind_mount')) {
                            await run(`mv ${config}/susfs_no_auto_add_sus_bind_mount /data/adb/`);
                        }
                        else {
                            await run(`rm -f /data/adb/susfs_no_auto_add_sus_bind_mount`);
                        }
                        if (LegitSusfsConfig.includes('susfs_no_auto_add_try_umount_for_bind_mount')) {
                            await run(`mv ${config}/susfs_no_auto_add_try_umount_for_bind_mount /data/adb/`);
                        }
                        else {
                            await run(`rm -f /data/adb/susfs_no_auto_add_try_umount_for_bind_mount`);
                        }
                        if (LegitSusfsConfig.includes('susfs_umount_for_zygote_system_process')) {
                            await run(`mv ${config}/susfs_umount_for_zygote_system_process /data/adb/`);
                        }
                        else {
                            await run(`rm -f /data/adb/susfs_umount_for_zygote_system_process`);
                        }
                    }
                    toast('Config imported successfully. Please reboot to apply changes.');
                    const settings = catToObject(await run(`cat ${config}/config.sh`));
                    set_uname(settings);
                    susfs_log_toggle(settings);
                    if (versionAtLeast(susfs_versions, 1, 5, 4)) auto_hide_settings(settings, susfs_features, susfs_versions);
                    sus_su_toggle(settings, susfs_versions);
                } catch (error) {
                    toast('Failed to import config: ' + error.message);
                }
                confirm_import_modal.close();
            });
            import_modal_backup.addEventListener('click', async () => {
                await export_susfs_config();
            });
        });
    });
}
