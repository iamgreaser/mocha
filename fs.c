#include "common.h"

void probe_filesystems(void)
{
	int i, j;

	int has_mounted_root = 1; // root is automounted by default

	int component_count = *(volatile uint8_t *)0xBFF00284;
	for(i = 0; i < component_count; i++)
	{
		*(volatile uint8_t *)0xBFF00284 = i;
		if(!strcmp((char *volatile)0xBFF00240, "filesystem"))
		{
			// mount it
			if(!has_mounted_root)
			{
				if(mount_filesystem((uint8_t *)0xBFF00200, "/") <0)
				{
					perror("mount_filesystem(root)");
					panic("Failed to mount root!");
				}

				has_mounted_root = 1;
			}

			char fnbuf[128];
			char fsshort[4];
			memcpy(fsshort, (uint8_t *)0xBFF00200, 3);
			fsshort[3] = '\x00';
			sprintf(fnbuf, "/mnt/%s/", fsshort);
			if(mount_filesystem((uint8_t *)0xBFF00200, fnbuf) < 0)
			{
				perror("mount_filesystem");
			}
		}
	}
}

