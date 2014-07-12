#include "ccextractor.h"
#include "cc_encoders_common.h"

void write_stringz_as_sami(char *string, struct encoder_ctx *context, LLONG ms_start, LLONG ms_end)
{
	int used;
    sprintf ((char *) str,
    		"<SYNC start=%llu><P class=\"UNKNOWNCC\">\r\n",
    		(unsigned long long)ms_start);
    if (ccx_options.encoding!=CCX_ENC_UNICODE)
    {
        dbg_print(CCX_DMT_608, "\r%s\n", str);
    }

	used = encode_line(context->buffer,(unsigned char *) str);
	write (context->out->fh, context->buffer, used);
    int len=strlen (string);
    unsigned char *unescaped= (unsigned char *) malloc (len+1); 
    unsigned char *el = (unsigned char *) malloc (len*3+1); // Be generous
    if (el==NULL || unescaped==NULL)
        fatal (EXIT_NOT_ENOUGH_MEMORY, "In write_stringz_as_sami() - not enough memory.\n");
    int pos_r=0;
    int pos_w=0;
    // Scan for \n in the string and replace it with a 0
    while (pos_r<len)
    {
        if (string[pos_r]=='\\' && string[pos_r+1]=='n')
        {
            unescaped[pos_w]=0;
            pos_r+=2;            
        }
        else
        {
            unescaped[pos_w]=string[pos_r];
            pos_r++;
        }
        pos_w++;
    }
    unescaped[pos_w]=0;
    // Now read the unescaped string (now several string'z and write them)    
    unsigned char *begin=unescaped;
    while (begin<unescaped+len)
    {
        unsigned int u = encode_line (el, begin);
        if (ccx_options.encoding!=CCX_ENC_UNICODE)
        {
            dbg_print(CCX_DMT_608, "\r");
            dbg_print(CCX_DMT_608, "%s\n",subline);
        }
		write(context->out->fh, el, u);
		write(context->out->fh, encoded_br, encoded_br_length);
        
		write(context->out->fh, encoded_crlf, encoded_crlf_length);
        begin+= strlen ((const char *) begin)+1;
    }

    sprintf ((char *) str,"</P></SYNC>\r\n");
    if (ccx_options.encoding!=CCX_ENC_UNICODE)
    {
        dbg_print(CCX_DMT_608, "\r%s\n", str);
    }
	used=encode_line (context->buffer,(unsigned char *) str);
	write(context->out->fh, context->buffer, used);
    sprintf ((char *) str,
    		"<SYNC start=%llu><P class=\"UNKNOWNCC\">&nbsp;</P></SYNC>\r\n\r\n",
    		(unsigned long long)ms_end);
    if (ccx_options.encoding!=CCX_ENC_UNICODE)
    {
        dbg_print(CCX_DMT_608, "\r%s\n", str);
    }
	write(context->out->fh, context->buffer, used);
}



int write_cc_buffer_as_sami(struct eia608_screen *data, struct encoder_ctx *context)
{
	int used;
	LLONG startms, endms;
    int wrote_something=0;
    startms = data->start_time;

    startms+=subs_delay;
    if (startms<0) // Drop screens that because of subs_delay start too early
        return 0; 

    endms   = data->end_time;
    endms--; // To prevent overlapping with next line.
    sprintf ((char *) str,
    		"<SYNC start=%llu><P class=\"UNKNOWNCC\">\r\n",
    		(unsigned long long)startms);
    if (ccx_options.encoding!=CCX_ENC_UNICODE)
    {
        dbg_print(CCX_DMT_608, "\r%s\n", str);
    }
	used = encode_line(context->buffer,(unsigned char *) str);
	write (context->out->fh, context->buffer, used);
    for (int i=0;i<15;i++)
    {
        if (data->row_used[i])
        {				
            int length = get_decoder_line_encoded (subline, i, data);
            if (ccx_options.encoding!=CCX_ENC_UNICODE)
            {
                dbg_print(CCX_DMT_608, "\r");
                dbg_print(CCX_DMT_608, "%s\n",subline);
            }
            write (context->out->fh, subline, length);            
            wrote_something=1;
            if (i!=14)            
                write (context->out->fh, encoded_br, encoded_br_length);            
            write (context->out->fh,encoded_crlf, encoded_crlf_length);
        }
    }
    sprintf ((char *) str,"</P></SYNC>\r\n");
    if (ccx_options.encoding!=CCX_ENC_UNICODE)
    {
        dbg_print(CCX_DMT_608, "\r%s\n", str);
    }
	used = encode_line(context->buffer,(unsigned char *) str);
	write (context->out->fh, context->buffer, used);
    sprintf ((char *) str,
    		"<SYNC start=%llu><P class=\"UNKNOWNCC\">&nbsp;</P></SYNC>\r\n\r\n",
    		(unsigned long long)endms);
    if (ccx_options.encoding!=CCX_ENC_UNICODE)
    {
        dbg_print(CCX_DMT_608, "\r%s\n", str);
    }
	used = encode_line(context->buffer,(unsigned char *) str);
	write (context->out->fh, context->buffer, used);
    return wrote_something;
}
