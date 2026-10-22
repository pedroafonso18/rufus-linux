/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - graphical front-end stub (used when GTK is not available).
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

#include "linux_workflow.h"

int linux_ui_available(void)
{
	return 0;
}

int linux_ui_main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	return -1;
}
