#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "dvd_subtitle_decoder.h"
#include "ccx_common_common.h"

#define MAX_BUFFERSIZE 4096 // arbitrary value

struct dvd_cc_data
{
	unsigned char *buffer; // Buffer to store packet data
	size_t len; //length of buffer required
	int pos; //position in the buffer
	uint16_t size_spu; //total size of spu packet
	uint16_t size_data; //size of data in the packet, offset to control packet
	struct ctrl_seq *ctrl;
	unsigned char *bitmap;
};

struct ctrl_seq
{
	uint8_t color[4];
	uint8_t alpha[4]; // alpha channel
	uint16_t coord[4];
	uint16_t pixoffset[2]; // Offset to 1st and 2nd graphic line 
	uint16_t start_time, stop_time;
};

#define bitoff(x) ((x) ? 0x0f : 0xf0)
#define next4(x,y) ((y) ? (x & 0x0f) : ((x & 0xf0) >> 4) )

// int get_bits(uint16_t temp, int n)
// {
// 	// Get n bits from 16 bits
// 	unsigned mask;
// 	mask = ((1 << n) - 1) << (16-n);
// 	return (temp & mask);
// }

int get_bits(struct dvd_cc_data *data, uint8_t *nextbyte, int *pos, int *m)
{
	int ret;
	ret = (*nextbyte & 0xf0) >> 4;
	if(*m == 0)
		*pos += 1;
	printf("n:%02x p:%d d:%02x m:%02x ", *nextbyte, *pos, data->buffer[*pos], bitoff(*m));
	*nextbyte = (*nextbyte << 4) | next4(data->buffer[*pos] , *m);
	*m = (*m + 1)%2;
	printf("nb:%02x ret:%x ", *nextbyte, ret);
	return ret;
}


int get_rlen(struct dvd_cc_data *data, int *color, uint8_t *nextbyte, int *pos, int *m)
{
	int val;
	uint16_t rlen;

	val = 4;
	rlen = get_bits(data, nextbyte, pos, m);
	printf("rlen:%x ", rlen);
	while(rlen < val && val <= 0x40)
	{
		rlen = (rlen << 4) | get_bits(data, nextbyte, pos, m);
		val = val << 2;
		printf("%x ", rlen);
	}
	*color = rlen & 0x3;
	rlen = rlen >> 2;
	printf("fin: %x color:%d\n", rlen, *color);
	return rlen;
}		


void rle_decode(struct dvd_cc_data *data)
{
	int w, h, x, lineno, pos, color, m, len;
	uint8_t nextbyte;
	unsigned char *buffp; // Copy of pointer to buffer to change

	// Calculate size
	w = (data->ctrl->coord[1] - data->ctrl->coord[0]) + 1;
	h = (data->ctrl->coord[3] - data->ctrl->coord[2]) + 1;
	dbg_print(CCX_DMT_VERBOSE, "w:%d h:%d\n", w, h);

	pos = data->ctrl->pixoffset[0];
	m = 0;
	nextbyte = data->buffer[pos];

	data->bitmap = malloc(w*h);
	buffp = data->bitmap;
	x = 0; lineno = 0;

	while(lineno < (h+1)/2)
	{
		printf("nextbyte:%02x ", nextbyte);
		len = get_rlen(data, &color, &nextbyte, &pos, &m);
		if(len > (w-x) || len == 0) // len is 0 if data is 0x000*
			len = w-x;
		memset(buffp + x, color, len);
		x+=len;
		if(x >= w)
		{
			// End of line
			x = 0;
			++lineno;
			buffp += (2*w); // Skip 1 line

			// Align byte at end of line
			if(bitoff(m) == 0x0f)
			{
				int discard = get_bits(data, &nextbyte, &pos, &m);
			}
		}	
	}

	// Lines are interlaced, for other half of alternate lines
	printf("posnow: %d\n", pos);
	if(pos > data->ctrl->pixoffset[1])
	{
		dbg_print(CCX_DMT_VERBOSE, "Error creating bitmap!");
		return;
	}

	pos = data->ctrl->pixoffset[1];
	buffp = data->bitmap + w; 
	x = 0; lineno = 0;

	while(lineno < h/2)
	{
		len = get_rlen(data, &color, &nextbyte, &pos, &m);
		if(len > (w-x) || len == 0) // len is 0 if data is 0x000*
			len = w-x;
		memset(buffp + x, color, len);
		x+=len;
		if(x >= w)
		{
			// End of line
			x = 0;
			++lineno;
			buffp += (2*w); // Skip 1 line

			// Align byte at end of line
			if(bitoff(m) == 0x0f)
			{
				int discard = get_bits(data, &nextbyte, &pos, &m);
			}
		}
	}
	// Data to be decoded: 06d206b30000 02d8 0000 02d8 0000 0200 a 0d0 0000 34 2e 08c 22 0a4 22 24 16 080 22 30 00 00 02ce 
	int i,j,c=0;

	for(i=0; i<h; ++i)
	{
		for(j=0;j<w;++j)
		{
			printf("%x", data->bitmap[c]);
			++c;
		}
		printf("\n");
	}
}


void decode_packet(struct dvd_cc_data *data)
{
	uint16_t date, next_ctrl;
	data->ctrl = malloc(sizeof(struct ctrl_seq));
	struct ctrl_seq *control = data->ctrl; 
	int command; // next command
	int seq_end, pack_end = 0;

	data->pos = data->size_data;
	dbg_print(CCX_DMT_VERBOSE, "In decode_packet()\n");

	while(data->pos <= data->len && pack_end == 0)
	{
		// Process control packet first
		date = (data->buffer[data->pos] << 8) | data->buffer[data->pos + 1];
		next_ctrl = (data->buffer[data->pos + 2] << 8) | data->buffer[data->pos + 3];
		if(next_ctrl == (data->pos))
		{
			// If it is the last control sequence, it points to itself
			pack_end = 1;
		}
		data->pos += 4;
		seq_end = 0;

		while(seq_end == 0)
		{
			command = data->buffer[data->pos];
			data->pos += 1;
			switch(command)
			{
				case 0x01:	control->start_time = date;
							break;
				case 0x02:	control->stop_time = date; 
							break;
				case 0x03:	// SET_COLOR
							control->color[0] = (data->buffer[data->pos] & 0xf0) >> 4;
							control->color[1] = data->buffer[data->pos] & 0x0f;
							control->color[2] = (data->buffer[data->pos + 1] & 0xf0) >> 4;
							control->color[3] = data->buffer[data->pos + 1] & 0x0f;
							dbg_print(CCX_DMT_VERBOSE, "col: %x col: %x col: %x col: %x\n", control->color[0], control->color[1], control->color[2], control->color[3]);
							data->pos+=2;
							break;
				case 0x04:	//SET_CONTR
							control->alpha[0] = (data->buffer[data->pos] & 0xf0) >> 4;
							control->alpha[1] = data->buffer[data->pos] & 0x0f;
							control->alpha[2] = (data->buffer[data->pos + 1] & 0xf0) >> 4;
							control->alpha[3] = data->buffer[data->pos + 1] & 0x0f;
							dbg_print(CCX_DMT_VERBOSE, "alp: %d alp: %d alp: %d alp: %d\n", control->alpha[0], control->alpha[1], control->alpha[2], control->alpha[3]);
							data->pos+=2;
							break;
				case 0x05:	//SET_DAREA
							control->coord[0] = ((data->buffer[data->pos] << 8) | (data->buffer[data->pos + 1] & 0xf0)) >> 4 ; //starting x coordinate
							control->coord[1] = ((data->buffer[data->pos + 1] & 0x0f) << 8 ) | data->buffer[data->pos + 2] ; //ending x coordinate
							control->coord[2] = ((data->buffer[data->pos + 3] << 8) | (data->buffer[data->pos + 4] & 0xf0)) >> 4 ; //starting y coordinate
							control->coord[3] = ((data->buffer[data->pos + 4] & 0x0f) << 8 ) | data->buffer[data->pos + 5] ; //ending y coordinate
							dbg_print(CCX_DMT_VERBOSE, "cord: %d cord: %d cord: %d cord: %d\n", control->coord[0], control->coord[1], control->coord[2], control->coord[3]);
							data->pos+=6;
							//(x2-x1+1)*(y2-y1+1)
							break;
				case 0x06:	//SET_DSPXA - Pixel address
							control->pixoffset[0] = (data->buffer[data->pos] << 8) | data->buffer[data->pos + 1];
							control->pixoffset[1] = (data->buffer[data->pos + 2] << 8) | data->buffer[data->pos + 3];
							dbg_print(CCX_DMT_VERBOSE, "off1: %d off2 %d\n", control->pixoffset[0], control->pixoffset[1]);
							data->pos+=4;
							break;
				case 0x07:	dbg_print(CCX_DMT_VERBOSE, "Command 0x07 found\n");
							uint16_t skip = (data->buffer[data->pos] << 8) | data->buffer[data->pos + 1];
							data->pos+=skip;
							break;
				case 0xff:	seq_end = 1;
							break;
				default:	dbg_print(CCX_DMT_VERBOSE, "Unknown command in control sequence!\n");
			}
		}
		
		// Decode data
		rle_decode(data);		

	}
}


int process_spu(unsigned char *buff, int length)
{
	struct dvd_cc_data *data = malloc( sizeof(struct dvd_cc_data) );
	data->buffer = buff;
	data->len = length;
	
	data->size_spu = (data->buffer[0] << 8) | data->buffer[1];
	if(data->size_spu > length)
	{
		// TODO: Data might be spread over several packets, handle this case to append to one buffer
		dbg_print(CCX_DMT_VERBOSE, "Data might be spread over several packets\n");
		return length;
	}

	if(data->size_spu != length)
	{
		dbg_print(CCX_DMT_VERBOSE, "SPU size mismatch\n");
		// return -1;
		return length; //FIXME: not the write thing to return
	}

	data->size_data = (data->buffer[2] << 8) | data->buffer[3];
	if(data->size_data > data->size_spu)
	{
		dbg_print(CCX_DMT_VERBOSE, "Invalid SPU Packet\n");
		// return -1;
		return length; //FIXME: not the write thing to return
	} 

	decode_packet(data);

	return length;
}

