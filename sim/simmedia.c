#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "simmedia.h"
#include "simdmk.h"


static const struct sim_media_ops *const media_ops[] = {
	&sim_dmk_ops,
};


struct sim_media *
sim_media_load(const char *path)
{
	const struct sim_media_ops *ops = media_ops[0];
	const char	*ext = strrchr(path, '.');

	if (ext) {
		++ext;

		for (size_t i = 0;
		     i < sizeof(media_ops) / sizeof(media_ops[0]); ++i) {
			if (!strcasecmp(ext, media_ops[i]->name)) {
				ops = media_ops[i];
				break;
			}
		}
	}

	return ops->load(path);
}


int
sim_media_eject(struct sim_media *media)
{
	int	ret = 0;

	if (media->dirty && !media->wp)
		ret = media->ops->save(media);

	media->ops->unload(media);

	return ret;
}
