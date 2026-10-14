import { toast } from 'kernelsu';
import { run, versionAtLeast } from '../../utils.js';
import { config } from '../../core/constants.js';

export function susfs_send_logs(){
	const susfs_send_logs_btn = document.getElementById('susfs_send_logs');
	var currentDate = new Date();
	var formattedDate = currentDate.getFullYear() + '-' +
		String(currentDate.getMonth() + 1).padStart(2, '0') + '-' +
		String(currentDate.getDate()).padStart(2, '0') + '_' +
		String(currentDate.getHours()).padStart(2, '0') + '-' +
		String(currentDate.getMinutes()).padStart(2, '0') + '-' +
		String(currentDate.getSeconds()).padStart(2, '0');
	susfs_send_logs_btn.addEventListener('click', async function(event) {
		try {
			await run(`cat /proc/$(pidof zygote64 | awk '{print $1}')/mountinfo > /data/adb/susfs4ksu_unified/state/zygote64_mountinfo.txt`);
			await run(`cat /proc/$(pidof zygote64 | awk '{print $1}')/maps > /data/adb/susfs4ksu_unified/state/zygote64_maps.txt`);
			await run(`cat /proc/1/mountinfo > /data/adb/susfs4ksu_unified/state/pid1_mountinfo.txt`);
			await run(`cp /data/adb/ksu/log/dmesg.log /data/adb/susfs4ksu_unified/state/dmesg.log`);
			await run(`ksud module list > /data/adb/susfs4ksu_unified/state/ksu_module_list.txt`);
			await run(`dmesg | grep susfs > /data/adb/susfs4ksu_unified/state/latest_dmesg_susfs.log`);
			await run(`tar -C /data/adb/susfs4ksu_unified/state/ -czvf /sdcard/Download/susfs_logs_${formattedDate}.tar.gz .`);
			toast("Logs saved to /sdcard/Download/susfs_logs_" + formattedDate + ".tar.gz");
			await run(`am start -a android.intent.action.SEND -t '*/*' -c android.intent.category.DEFAULT --eu android.intent.extra.STREAM 'file:///sdcard/Download/susfs_logs_${formattedDate}.tar.gz'`);
		} catch (error) {
			toast("Failed to prepare logs: " + error.message);
		}
	});
}

export function susfs_export_config(susfs_versions){
	const susfs_export_config_btn = document.getElementById('susfs_export');
	susfs_export_config_btn.addEventListener('click', async function(event) {
		await export_susfs_config(susfs_versions);
	});
}

export async function export_susfs_config(susfs_versions) {
	var is_no_auto_mount = await run(`[ -f data/adb/susfs_no_auto_add_sus_ksu_default_mount ] && echo true || echo false`);
	var is_no_auto_bind = await run(`[ -f data/adb/susfs_no_auto_add_sus_bind_mount ] && echo true || echo false`);
	var is_no_auto_umount_bind = await run(`[ -f data/adb/susfs_no_auto_add_try_umount_for_bind_mount ] && echo true || echo false`);
	var is_try_umount_zygote = await run(`[ -f data/adb/susfs_umount_for_zygote_system_process ] && echo true || echo false`);
	try{
		var currentDate = new Date();
		var formattedDate = currentDate.getFullYear() + '-' +
			String(currentDate.getMonth() + 1).padStart(2, '0') + '-' +
			String(currentDate.getDate()).padStart(2, '0') + '_' +
			String(currentDate.getHours()).padStart(2, '0') + '-' +
			String(currentDate.getMinutes()).padStart(2, '0') + '-' +
			String(currentDate.getSeconds()).padStart(2, '0');
		if (versionAtLeast(susfs_versions, 1, 5, 3) && !versionAtLeast(susfs_versions, 2, 0, 0)) {
			if (is_no_auto_mount === "true") {
				await run(`cp /data/adb/susfs_no_auto_add_sus_ksu_default_mount ${config}/`);
			}
			if (is_no_auto_bind === "true") {
				await run(`cp /data/adb/susfs_no_auto_add_sus_bind_mount ${config}/`);
			}
			if (is_no_auto_umount_bind === "true") {
				await run(`cp /data/adb/susfs_no_auto_add_try_umount_for_bind_mount ${config}/`);
			}
			if (is_try_umount_zygote === "true") {
				await run(`cp /data/adb/susfs_umount_for_zygote_system_process ${config}/`);
			}
		}
		await run(`tar -C ${config}/ -czvf /sdcard/Download/susfs_settings_${formattedDate}.tar.gz .`);
		toast("Settings exported to /sdcard/Download/susfs_settings_" + formattedDate + ".tar.gz");
		if (versionAtLeast(susfs_versions, 1, 5, 3) && !versionAtLeast(susfs_versions, 2, 0, 0)) {
			if (is_no_auto_mount === "true") {
				await run(`rm -f ${config}/susfs_no_auto_add_sus_ksu_default_mount`);
			}
			if (is_no_auto_bind === "true") {
				await run(`rm -f ${config}/susfs_no_auto_add_sus_bind_mount`);
			}
			if (is_no_auto_umount_bind === "true") {
				await run(`rm -f ${config}/susfs_no_auto_add_try_umount_for_bind_mount`);
			}
			if (is_try_umount_zygote === "true") {
				await run(`rm -f ${config}/susfs_umount_for_zygote_system_process`);
			}
		}
	}
	catch{
		toast("Failed to export settings");
	}
}