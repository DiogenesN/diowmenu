/* Give a path containting .desktop files and it will extract Name, Exec, Icon data
 * Usage:
 * char *names[2048];
 * char *execs[2048];
 * char *icons[2048];
 * process_directory("/usr/share/applications");
 * at the end of function 'process_desktop_file' populate your arrays like this:
 * names[item_counter] = strdup(nameOfAppNoWhite);
 * execs[item_counter] = strdup(nameOfExecNoWhite);
 * icons[item_counter] = strdup(nameOfIconNoWhite);
 * Don't forget to clear resources:
 * for (int i = 0; names[i] != NULL; i++) {
 *		free(names[i]);
 *		free(execs[i]);
 *		free(icons[i]);
 *	}
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "externvars.h"
#include "getstrfromsubstrinfile.h"

/* Takes a /path/name.desktop as argument and gets the Name=, Exec=, Icon= strings */
static void process_desktop_file(const char *filename) {
	int counter_name = 0;
	int counter_exec = 0;
	int counter_icon = 0;
	char buffer[512];
	char nameOfApp[512] = "none";
	char execOfApp[512] = "none";
	char iconOfApp[512] = "none";
	const char *searchName = "Name";
	const char *searchExec = "Exec";
	const char *searchIcon = "Icon";

	// Gtting path to noicon
	const char *HOME = getenv("HOME");
	if (HOME == NULL) {
		fprintf(stderr, "Unable to determine the user's home directory.\n");
		return;
	}
	const char *noicon	= "/.config/diowmenu/noicon.svg";
	const char *iconsCache	= "/.config/diowmenu/icons.cache";
	char noiconBuff[strlen(HOME) + strlen(noicon) + 1];
	char iconsCacheBuff[strlen(HOME) + strlen(iconsCache) + 1];
	snprintf(noiconBuff, sizeof(noiconBuff), "%s%s", HOME, noicon);
	snprintf(iconsCacheBuff, sizeof(iconsCacheBuff), "%s%s", HOME, iconsCache);
	
	FILE *textFile = fopen(filename, "r");

	if (textFile == NULL) {
		perror("Error opening file");
		return;
	}

	while (fgets(buffer, sizeof(buffer), textFile) != NULL) {
		const char *pos = strchr(buffer, '='); // holds str after = sign
		if (strstr(buffer, searchName) != NULL && // if current line contains Name=
			strstr(buffer, "#") == NULL && // and the line doesn't start with #
			strstr(buffer, "Name[") == NULL && // and it doesn't start with Name[
			strstr(buffer, "GenericName") == NULL && // and it doesn't start with GenericName 
			counter_name < 1) { // and it finds only the first occurrence of Name

			counter_name = counter_name + 1;
			if (pos != NULL) {
				if (isspace(pos[1])) {
					pos++;
					strcpy(nameOfApp, pos + 1); // copies the content after = sign
					nameOfApp[strlen(nameOfApp) - 1] = '\0'; // Remove newline 
				}
				else {
					strcpy(nameOfApp, pos + 1); // copies the content after = sign
					nameOfApp[strlen(nameOfApp) - 1] = '\0'; // Remove newline 
				}
			}
		}
		if (strstr(buffer, searchExec) != NULL && \
			strstr(buffer, "#") == NULL && \
			strstr(buffer, "TryExec") == NULL && \
			strstr(buffer, "#Exec") == NULL && \
			counter_exec < 1) {
			counter_exec = counter_exec + 1;
			if (pos != NULL) {
				// Find the '%' character in the string
				// removes '%' and what comes after it because e.g. thunar %U won't run
				char *percentPos = strchr(pos, '%');
				char *quotePos = strchr(pos, '"');
				if (percentPos != NULL && quotePos == NULL) {
					// Truncate the string at the '%' character
					*percentPos = '\0';
				}

				if (isspace(pos[1])) {
					pos++;
					strcpy(execOfApp, pos + 1);
					execOfApp[strlen(execOfApp) - 1] = '\0'; // Remove newline 
				}
				else {
					strcpy(execOfApp, pos + 1);
					execOfApp[strlen(execOfApp) - 1] = '\0'; // Remove newline 
				}
			}
		}

		if (strstr(buffer, searchIcon) != NULL && \
			strstr(buffer, "#") == NULL && \
			counter_icon < 1) {
			counter_icon = counter_icon + 1;
			if (pos != NULL) {
				strcpy(iconOfApp, pos + 1);
				iconOfApp[strlen(iconOfApp) - 1] = '\0'; // Remove newline 
			}
		}
	}

	fclose(textFile);

	// Getting rid of nasty whitespace that doesn't properly get items if file contains Icon = ...
	char nameOfAppNoWhite[512];
	char nameOfExecNoWhite[512];
	char nameOfIconNoWhite[512];

	// Name whitespace remove
	const char *ptrNameIndex = nameOfApp;
	while (isspace(*ptrNameIndex)) {
		ptrNameIndex++;
	}
	strcpy(nameOfAppNoWhite, ptrNameIndex);

	// Exec whitespace remove
	ptrNameIndex = execOfApp;
	while (isspace(*ptrNameIndex)) {
		ptrNameIndex++;
	}
	strcpy(nameOfExecNoWhite, ptrNameIndex);

	// Icon whitespace remove
	ptrNameIndex = iconOfApp;
	while (isspace(*ptrNameIndex)) {
		ptrNameIndex++;
	}
	strcpy(nameOfIconNoWhite, ptrNameIndex);

	// Check if any field is empty before printing
	if (strlen(nameOfAppNoWhite) > 0 && strlen(nameOfExecNoWhite) > 0 && \
																strlen(nameOfIconNoWhite) > 0) {
		// Check if icon.cache is empty
		const char *iconPath = find_substring_in_file(iconsCacheBuff, iconOfApp);
		snprintf(nameOfIconNoWhite, strlen(iconPath) + 1, "%s", iconPath);

		///printf("item_counter: %d\n", item_counter);
		///printf("name: %s\n", nameOfAppNoWhite);
		names[item_counter] = strdup(nameOfAppNoWhite);
		///printf("exec: %s\n", nameOfExecNoWhite);
		execs[item_counter] = strdup(nameOfExecNoWhite);
		///printf("icon: %s\n", nameOfIconNoWhite);
		icons[item_counter] = strdup(nameOfIconNoWhite);
		///printf("\n");

		free((void *)iconPath);
		iconPath = NULL;
		item_counter = item_counter + 1;
	}
}

/* Getting one .desktop file at a time from the given directory path */
void process_directory(const char *directoryPath) {
	DIR *dirPath = opendir(directoryPath);
	char filepath[2048];
	if (dirPath == NULL) {
		perror("Error opening directory");
		return;
	}
	struct dirent *listDirFiles;
	while ((listDirFiles = readdir(dirPath)) != NULL) {
		// DT_REG selecting only regular text files
		if (listDirFiles->d_type == DT_REG && strcmp(listDirFiles->d_name + \
							strlen(listDirFiles->d_name) - strlen(".desktop"), ".desktop") == 0) {
			snprintf(filepath, sizeof(filepath), "%s/%s", directoryPath, listDirFiles->d_name);
			// Calling the process_desktop_file func with /path/name.desktop
			process_desktop_file(filepath);
		}
	}
	closedir(dirPath);
}
