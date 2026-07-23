#ifndef GWENCODE_H
#define GWENCODE_H

#ifdef __cplusplus
extern "C" {
#endif

//#include <stdint.h>
//#include <stdbool.h>
//#include <stdio.h>
//#include <string.h>


/*
 * Values for return from (*encode_pulse):
 *   negative: error (stop)
 *   0       : continue
 *   positive: done (stop)
 */

struct dmk_encode_s {
	int	(*encode_pulse)(uint32_t pulse, void *data);
	void	*pulse_data;
};


#ifdef __cplusplus
}
#endif

#endif
