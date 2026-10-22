/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - GTK graphical front-end.
 * Copyright © 2011-2026 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The GTK front-end. It mirrors the layout of the Windows version of Rufus:
 * a device selector, the image selection, the partition/file system options
 * and a log window.
 */

#include <gtk/gtk.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linux_api.h"
#include "linux_workflow.h"
#include "posix/win_compat.h"

typedef struct {
	GtkWidget* window;
	GtkWidget* device_combo;
	GtkWidget* iso_radio;
	GtkWidget* image_radio;
	GtkWidget* select_button;
	GtkWidget* image_label;
	GtkWidget* partition_combo;
	GtkWidget* target_combo;
	GtkWidget* fs_combo;
	GtkWidget* cluster_combo;
	GtkWidget* label_entry;
	GtkWidget* log_view;
	GtkWidget* progress_bar;
	GtkWidget* status_label;
	GtkWidget* start_button;
	GtkWidget* device_label;
	GtkWidget* show_all_check;
	GtkWidget* scheme_label;
	GtkWidget* target_label;
	GtkWidget* fs_label;
	GtkWidget* cluster_label;
	GtkWidget* volume_label;
	GtkListStore* device_store;
	GThread* worker;
	gboolean operation_running;
	char image_path[4096];
	char image_name[256];
} rufus_ui;

static rufus_ui ui;

/* The log is appended to from the worker thread, so we marshal the updates
 * to the main loop with g_idle_add(). */
typedef struct {
	char* text;
} log_msg;

typedef struct {
	int percent;
	char* text;
} progress_msg;

static gboolean append_log(gpointer data)
{
	log_msg* msg = (log_msg*)data;
	GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ui.log_view));
	if (buffer != NULL)
		gtk_text_buffer_insert_at_cursor(buffer, msg->text, -1);
	g_free(msg->text);
	g_free(msg);
	return G_SOURCE_REMOVE;
}

static gboolean update_progress(gpointer data)
{
	progress_msg* msg = (progress_msg*)data;
	if (msg->text != NULL) {
		gtk_label_set_text(GTK_LABEL(ui.status_label), msg->text);
		g_free(msg->text);
	}
	if (msg->percent >= 0)
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.progress_bar), msg->percent / 100.0);
	g_free(msg);
	return G_SOURCE_REMOVE;
}

/*
 * The Win32 compatibility layer routes the Rufus log and progress messages
 * through these callbacks.
 */
static LRESULT gui_message(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	(void)hWnd; (void)wParam; (void)lParam;
	switch (Msg) {
	case UM_PROGRESS_INIT:
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.progress_bar), 0.0);
		break;
	case UM_PROGRESS_EXIT:
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.progress_bar), 0.0);
		break;
	case UM_FORMAT_COMPLETED:
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.progress_bar),
			(wParam != 0) ? 1.0 : 0.0);
		break;
	default:
		break;
	}
	return 0;
}

static int gui_message_box(HWND hWnd, const char* text, const char* caption, UINT type)
{
	GtkWidget* dialog;
	int response;

	(void)hWnd;
	dialog = gtk_message_dialog_new(GTK_WINDOW(ui.window), GTK_DIALOG_MODAL,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, "%s", text != NULL ? text : "");
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	(void)caption; (void)type;
	return (response == GTK_RESPONSE_OK) ? IDYES : IDNO;
}

static void gui_log(void* context, const char* text)
{
	log_msg* msg = (log_msg*)g_malloc(sizeof(log_msg));
	msg->text = g_strdup(text != NULL ? text : "");
	g_idle_add(append_log, msg);
}

static void gui_progress(void* context, int percent, const char* text)
{
	progress_msg* msg = (progress_msg*)g_malloc(sizeof(progress_msg));
	msg->percent = percent;
	msg->text = (text != NULL) ? g_strdup(text) : NULL;
	g_idle_add(update_progress, msg);
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static void append_log_line(const char* format, ...)
{
	char buffer[8192];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	gui_log(NULL, buffer);
}

static const char* selected_fs(void)
{
	const char* fs = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(ui.fs_combo));
	return (fs != NULL) ? fs : "fat32";
}

static void update_sensitivity(void)
{
	gboolean running = ui.operation_running;
	gboolean has_image = (ui.image_path[0] != 0);
	gboolean has_device = (gtk_combo_box_get_active(GTK_COMBO_BOX(ui.device_combo)) >= 0);

	gtk_widget_set_sensitive(ui.select_button, !running);
	gtk_widget_set_sensitive(ui.start_button, !running && has_image && has_device);
	gtk_widget_set_sensitive(ui.device_combo, !running);
	gtk_widget_set_sensitive(ui.partition_combo, !running);
	gtk_widget_set_sensitive(ui.target_combo, !running);
	gtk_widget_set_sensitive(ui.fs_combo, !running);
	gtk_widget_set_sensitive(ui.cluster_combo, !running);
	gtk_widget_set_sensitive(ui.label_entry, !running);
	gtk_widget_set_sensitive(ui.iso_radio, !running);
	gtk_widget_set_sensitive(ui.image_radio, !running);
}

/* Populate the file system list, according to the selected target system */
static void refresh_file_systems(void)
{
	const char* target = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(ui.target_combo));
	int i;

	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui.fs_combo));
	if (target != NULL && (strcmp(target, "BIOS") == 0)) {
		/* A BIOS target needs a file system that Windows can boot from */
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "Large FAT32");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "FAT16");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "NTFS");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "UDF");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "ext2");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "ext3");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "ext4");
	} else {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "Large FAT32");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "NTFS");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "UDF");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "ext2");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "ext3");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fs_combo), "ext4");
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(ui.fs_combo), 0);
	(void)i;
}

/* Refresh the list of devices */
static void refresh_devices(void)
{
	DWORD i;
	int index;

	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(ui.device_combo));
	linux_refresh_devices();
	for (i = 0; i < MAX_DRIVES; i++) {
		char label[512];
		char size[32];
		if (rufus_drive[i].id == NULL)
			continue;
		snprintf(size, sizeof(size), "%.1f GB", rufus_drive[i].size / 1073741824.0);
		snprintf(label, sizeof(label), "%s (%s)", rufus_drive[i].name != NULL ?
			rufus_drive[i].name : rufus_drive[i].id, size);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.device_combo), label);
	}
	index = gtk_combo_box_get_active(GTK_COMBO_BOX(ui.device_combo));
	(void)index;
	update_sensitivity();
}

/* ------------------------------------------------------------------------ */
/* The worker thread                                                        */
/* ------------------------------------------------------------------------ */

static gpointer write_thread(gpointer data)
{
	linux_options opts;
	int drive_index;

	(void)data;
	memset(&opts, 0, sizeof(opts));
	opts.image = ui.image_path;
	opts.label = gtk_entry_get_text(GTK_ENTRY(ui.label_entry));
	opts.fs = selected_fs();
	opts.partition_style = "gpt";
	{
		const char* scheme = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(ui.partition_combo));
		if (scheme != NULL && strcmp(scheme, "MBR") == 0)
			opts.partition_style = "mbr";
	}
	{
		const char* target = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(ui.target_combo));
		opts.use_uefi = (target == NULL) || (strcmp(target, "BIOS") != 0);
	}
	opts.use_bios = !opts.use_uefi;

	drive_index = gtk_combo_box_get_active(GTK_COMBO_BOX(ui.device_combo));
	if (drive_index < 0) {
		append_log_line("ERROR: No device selected.");
		g_idle_add(update_progress, NULL);
		return NULL;
	}
	opts.device = rufus_drive[drive_index].id;

	linux_write_image(&opts);
	return NULL;
}

static void on_start_clicked(GtkWidget* widget, gpointer data)
{
	(void)widget; (void)data;
	if (ui.operation_running)
		return;
	if (ui.image_path[0] == 0) {
		gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
			"Please select an image first.");
		return;
	}
	ui.operation_running = TRUE;
	update_sensitivity();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.progress_bar), 0.0);
	ui.worker = g_thread_new("rufus-write", write_thread, NULL);
}

static void on_select_clicked(GtkWidget* widget, gpointer data)
{
	GtkWidget* chooser;
	char* filename;

	(void)widget; (void)data;
	chooser = gtk_file_chooser_dialog_new("Select Disk Image", GTK_WINDOW(ui.window),
		GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
		"_Open", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), FALSE);
	{
		GtkFileFilter* filter = gtk_file_filter_new();
		gtk_file_filter_set_name(filter, "Disk images");
		gtk_file_filter_add_pattern(filter, "*.iso");
		gtk_file_filter_add_pattern(filter, "*.img");
		gtk_file_filter_add_pattern(filter, "*.bin");
		gtk_file_filter_add_pattern(filter, "*.raw");
		gtk_file_filter_add_pattern(filter, "*.vhd");
		gtk_file_filter_add_pattern(filter, "*.vmdk");
		gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
		gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser), filter);
	}
	if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
		if (filename != NULL) {
			snprintf(ui.image_path, sizeof(ui.image_path), "%s", filename);
			snprintf(ui.image_name, sizeof(ui.image_name), "%s",
				strrchr(filename, '/') != NULL ? strrchr(filename, '/') + 1 : filename);
			gtk_label_set_text(GTK_LABEL(ui.image_label), ui.image_name);
			g_free(filename);
			update_sensitivity();
		}
	}
	gtk_widget_destroy(chooser);
}

static void on_target_changed(GtkWidget* widget, gpointer data)
{
	(void)widget; (void)data;
	refresh_file_systems();
}

static void on_show_all_toggled(GtkWidget* widget, gpointer data)
{
	(void)widget; (void)data;
	enable_HDDs = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.show_all_check));
	refresh_devices();
}

static void on_device_changed(GtkWidget* widget, gpointer data)
{
	int index = gtk_combo_box_get_active(GTK_COMBO_BOX(ui.device_combo));
	(void)widget; (void)data;
	if (index >= 0) {
		char size[128];
		usb_device_props props;
		memset(&props, 0, sizeof(props));
		GetUSBProperties(rufus_drive[index].id, &props);
		snprintf(size, sizeof(size), "%s - %.1f GB%s",
			rufus_drive[index].id, rufus_drive[index].size / 1073741824.0,
			props.is_USB ? "" : " (internal disk)");
		gtk_label_set_text(GTK_LABEL(ui.device_label), size);
	}
	update_sensitivity();
}

/* ------------------------------------------------------------------------ */
/* Construction                                                              */
/* ------------------------------------------------------------------------ */

static GtkWidget* add_label_row(GtkWidget* grid, int row, const char* text)
{
	GtkWidget* label = gtk_label_new(text);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
	return label;
}

static void build_ui(void)
{
	GtkWidget* grid;
	GtkWidget* frame;
	GtkWidget* scrolled;
	GtkWidget* box;
	int row = 0;

	ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(ui.window), "Rufus: The Reliable USB Formatting Utility");
	gtk_window_set_default_size(GTK_WINDOW(ui.window), 760, 620);
	gtk_container_set_border_width(GTK_CONTAINER(ui.window), 8);
	g_signal_connect(ui.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_add(GTK_CONTAINER(ui.window), box);

	grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);

	/* Device */
	add_label_row(grid, row, "Device:");
	ui.device_combo = gtk_combo_box_text_new();
	gtk_widget_set_hexpand(ui.device_combo, TRUE);
	gtk_grid_attach(GTK_GRID(grid), ui.device_combo, 1, row, 1, 1);
	ui.show_all_check = gtk_check_button_new_with_label("Show all devices");
	gtk_widget_set_tooltip_text(ui.show_all_check,
		"Also list the internal disks, which is not recommended");
	gtk_grid_attach(GTK_GRID(grid), ui.show_all_check, 2, row, 1, 1);
	g_signal_connect(ui.show_all_check, "toggled", G_CALLBACK(on_show_all_toggled), NULL);
	g_signal_connect(ui.device_combo, "changed", G_CALLBACK(on_device_changed), NULL);
	row++;
	ui.device_label = gtk_label_new("");
	gtk_widget_set_halign(ui.device_label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), ui.device_label, 1, row, 2, 1);
	row++;

	/* Boot selection */
	frame = gtk_frame_new("Boot selection");
	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);
	{
		GtkWidget* inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		GtkWidget* group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
		gtk_container_add(GTK_CONTAINER(frame), inner);
		ui.iso_radio = gtk_radio_button_new_with_label(NULL, "ISO Image mode");
		ui.image_radio = gtk_radio_button_new_with_label_from_widget(
			GTK_RADIO_BUTTON(ui.iso_radio), "Disk image mode");
		gtk_box_pack_start(GTK_BOX(group), ui.iso_radio, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(group), ui.image_radio, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(inner), group, FALSE, FALSE, 0);
		ui.select_button = gtk_button_new_with_label("SELECT");
		gtk_box_pack_start(GTK_BOX(inner), ui.select_button, FALSE, FALSE, 0);
		g_signal_connect(ui.select_button, "clicked", G_CALLBACK(on_select_clicked), NULL);
		ui.image_label = gtk_label_new("(No image selected)");
		gtk_widget_set_hexpand(ui.image_label, TRUE);
		gtk_box_pack_start(GTK_BOX(inner), ui.image_label, TRUE, TRUE, 0);
	}

	/* Options */
	grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	row = 0;

	add_label_row(grid, row, "Partition scheme:");
	ui.partition_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.partition_combo), "GPT");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.partition_combo), "MBR");
	gtk_combo_box_set_active(GTK_COMBO_BOX(ui.partition_combo), 0);
	gtk_grid_attach(GTK_GRID(grid), ui.partition_combo, 1, row, 1, 1);
	ui.scheme_label = gtk_label_new("");
	gtk_widget_set_halign(ui.scheme_label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), ui.scheme_label, 2, row, 1, 1);
	row++;

	add_label_row(grid, row, "Target system:");
	ui.target_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.target_combo), "UEFI (non CSM)");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.target_combo), "BIOS or UEFI");
	gtk_combo_box_set_active(GTK_COMBO_BOX(ui.target_combo), 0);
	gtk_grid_attach(GTK_GRID(grid), ui.target_combo, 1, row, 1, 1);
	g_signal_connect(ui.target_combo, "changed", G_CALLBACK(on_target_changed), NULL);
	ui.target_label = gtk_label_new("");
	gtk_widget_set_halign(ui.target_label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), ui.target_label, 2, row, 1, 1);
	row++;

	add_label_row(grid, row, "File system:");
	ui.fs_combo = gtk_combo_box_text_new();
	gtk_grid_attach(GTK_GRID(grid), ui.fs_combo, 1, row, 1, 1);
	ui.fs_label = gtk_label_new("");
	gtk_widget_set_halign(ui.fs_label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), ui.fs_label, 2, row, 1, 1);
	row++;

	add_label_row(grid, row, "Cluster size:");
	ui.cluster_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.cluster_combo), "Default");
	gtk_combo_box_set_active(GTK_COMBO_BOX(ui.cluster_combo), 0);
	gtk_grid_attach(GTK_GRID(grid), ui.cluster_combo, 1, row, 1, 1);
	ui.cluster_label = gtk_label_new("");
	gtk_widget_set_halign(ui.cluster_label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), ui.cluster_label, 2, row, 1, 1);
	row++;

	add_label_row(grid, row, "Volume label:");
	ui.label_entry = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(ui.label_entry), 16);
	gtk_entry_set_text(GTK_ENTRY(ui.label_entry), "RUFUS");
	gtk_grid_attach(GTK_GRID(grid), ui.label_entry, 1, row, 2, 1);
	ui.volume_label = gtk_label_new("");
	gtk_widget_set_halign(ui.volume_label, GTK_ALIGN_START);
	gtk_grid_attach(GTK_GRID(grid), ui.volume_label, 2, row, 1, 1);
	row++;

	/* Log */
	frame = gtk_frame_new("Log");
	gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 0);
	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_vexpand(scrolled, TRUE);
	gtk_container_add(GTK_CONTAINER(frame), scrolled);
	ui.log_view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(ui.log_view), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(ui.log_view), TRUE);
	gtk_container_add(GTK_CONTAINER(scrolled), ui.log_view);

	/* Status bar and start button */
	{
		GtkWidget* status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_box_pack_start(GTK_BOX(box), status_box, FALSE, FALSE, 0);
		ui.status_label = gtk_label_new("Ready");
		gtk_widget_set_halign(ui.status_label, GTK_ALIGN_START);
		gtk_widget_set_hexpand(ui.status_label, TRUE);
		gtk_box_pack_start(GTK_BOX(status_box), ui.status_label, TRUE, TRUE, 0);
		ui.start_button = gtk_button_new_with_label("START");
		gtk_box_pack_start(GTK_BOX(status_box), ui.start_button, FALSE, FALSE, 0);
		g_signal_connect(ui.start_button, "clicked", G_CALLBACK(on_start_clicked), NULL);
	}

	ui.progress_bar = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(box), ui.progress_bar, FALSE, FALSE, 0);

	refresh_file_systems();
	refresh_devices();
	update_sensitivity();
}

/* ------------------------------------------------------------------------ */
/* Entry point                                                              */
/* ------------------------------------------------------------------------ */

int linux_ui_available(void)
{
	/* The graphical interface requires a display server */
	return (getenv("DISPLAY") != NULL) || (getenv("WAYLAND_DISPLAY") != NULL);
}

int linux_ui_main(int argc, char** argv)
{
	win_dispatch dispatch;

	gtk_init(&argc, &argv);
	build_ui();

	memset(&dispatch, 0, sizeof(dispatch));
	dispatch.message = gui_message;
	dispatch.message_box = gui_message_box;
	dispatch.progress = gui_progress;
	dispatch.log = gui_log;
	win_set_dispatch(&dispatch);

	gtk_widget_show_all(ui.window);
	gtk_main();
	return 0;
}
