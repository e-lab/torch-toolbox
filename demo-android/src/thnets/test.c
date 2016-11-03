#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "thnets.h"

typedef struct {
	char filename[255];
	unsigned char *bitmap;
	int width, height, cp;
} img_t;

int loadimage(const char *path, img_t *image);

int main(int argc, char **argv)
{
	THNETWORK *net;
	float *result;
	int i, rc, outwidth, outheight;
	
	if(argc != 3)
	{
		fprintf(stderr, "Syntax: loadtorch <models directory> <input file>\n");
		return -1;
	}
	net = THLoadNetwork(argv[1]);
	if(net)
	{
		THMakeSpatial(net);
		if(strstr(argv[2], ".t7"))
		{
			struct thobject input_o;
			
			rc = loadtorch(argv[2], &input_o, 8);
			if(!rc)
			{
				THFloatTensor *in = THFloatTensor_newFromObject(&input_o);
				rc = THProcessFloat(net, in->storage->data, 1, in->size[2], in->size[1], &result, &outwidth, &outheight);
				for(i = 0; i < rc; i++)
					printf("(%d,%d,%d): %f\n", i/(outwidth*outheight), i % (outwidth*outheight) / outwidth, i % outwidth, result[i]);
				THFloatTensor_free(in);
				freeobject(&input_o);
			} else printf("Error loading %s\n", argv[2]);
		} else {
			img_t image;
			
			rc = loadimage(argv[2], &image);
			if(!rc)
			{
				rc = THProcessImages(net, &image.bitmap, 1, image.width, image.height, 3*image.width, &result, &outwidth, &outheight);
				for(i = 0; i < rc; i++)
					printf("(%d,%d,%d): %f\n", i/(outwidth*outheight), i % (outwidth*outheight) / outwidth, i % outwidth, result[i]);
				free(image.bitmap);
			} else printf("Error loading image %s\n", argv[2]);
		}
		THFreeNetwork(net);
	} else printf("The network could not be loaded: %d\n", THLastError());
#ifdef MEMORYDEBUG
	debug_memorydump(stderr);
#endif
	return 0;
}
