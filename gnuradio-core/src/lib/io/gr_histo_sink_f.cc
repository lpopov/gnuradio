/* -*- c++ -*- */
/*
 * Copyright 2009,2010 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gr_histo_sink_f.h>
#include <gr_io_signature.h>

static float get_clean_num(float num){
  if (num == 0) return 0;
  /* extract sign and exponent from num */
  int sign = (num < 0) ? -1 : 1; num = fabs(num);
  float exponent = floor(log10(num));
  /* search for closest number with base 1, 2, 5, 10 */
  float closest_num = 10*pow(10, exponent);
  if (fabs(num - 1*pow(10, exponent)) < fabs(num - closest_num))
    closest_num = 1*pow(10, exponent);
  if (fabs(num - 2*pow(10, exponent)) < fabs(num - closest_num))
    closest_num = 2*pow(10, exponent);
  if (fabs(num - 5*pow(10, exponent)) < fabs(num - closest_num))
    closest_num = 5*pow(10, exponent);
  return sign*closest_num;
}

gr_histo_sink_f_sptr
gr_make_histo_sink_f (gr_msg_queue_sptr msgq)
{
  return gnuradio::get_initial_sptr(new gr_histo_sink_f (msgq));
}

gr_histo_sink_f::gr_histo_sink_f (gr_msg_queue_sptr msgq)
  : gr_sync_block ("histo_sink_f", gr_make_io_signature (1, 1, sizeof (float)), gr_make_io_signature (0, 0, 0)),
  d_msgq (msgq), d_num_bins(11), d_frame_size(1000), d_sample_count(0), d_bins(NULL), d_samps(NULL)
{
  //allocate arrays and clear
  set_num_bins(d_num_bins);
  set_frame_size(d_frame_size);
}

gr_histo_sink_f::~gr_histo_sink_f (void)
{
  delete [] d_samps;
  delete [] d_bins;
}

int
gr_histo_sink_f::work (int noutput_items,
  gr_vector_const_void_star &input_items,
  gr_vector_void_star &output_items)
{
  const float *in = (const float *) input_items[0];
  gruel::scoped_lock guard(d_mutex);	// hold mutex for duration of this function
  for (unsigned int i = 0; i < (unsigned int)noutput_items; i++){
    d_samps[d_sample_count] = in[i];
    d_sample_count++;
    /* processed a frame? */
    if (d_sample_count == d_frame_size){
      send_frame();
      clear();
    }
  }
  return noutput_items;
}

void
gr_histo_sink_f::send_frame(void){
  /* output queue full, drop the data */
  if (d_msgq->full_p()) return;
  /* find the minimum and maximum */
  float minimum = d_samps[0];
  float maximum = d_samps[0];
  for (unsigned int i = 0; i < d_frame_size; i++){
    if (d_samps[i] < minimum) minimum = d_samps[i];
    if (d_samps[i] > maximum) maximum = d_samps[i];
  }
  minimum = get_clean_num(minimum);
  maximum = get_clean_num(maximum);
  if (minimum == maximum || minimum > maximum) return; //useless data or screw up?
  /* load the bins */
  int index;
  float bin_width = (maximum - minimum)/(d_num_bins-1);
  for (unsigned int i = 0; i < d_sample_count; i++){
    index = round((d_samps[i] - minimum)/bin_width);
    /* ensure the index range in case a small floating point error is involed */
    if (index < 0) index = 0;
    if (index >= (int)d_num_bins) index = d_num_bins-1;
    d_bins[index]++;
  }
  /* Build a message to hold the output records */
  gr_message_sptr msg = gr_make_message(0, minimum, maximum, d_num_bins*sizeof(float));
  float *out = (float *)msg->msg(); // get pointer to raw message buffer
  /* normalize the bins and put into message */
  for (unsigned int i = 0; i < d_num_bins; i++){
    out[i] = ((float)d_bins[i])/d_frame_size;
  }
  /* send the message */
  d_msgq->handle(msg);
}

void
gr_histo_sink_f::clear(void){
  d_sample_count = 0;
  /* zero the bins */
  for (unsigned int i = 0; i < d_num_bins; i++){
    d_bins[i] = 0;
  }
}

/**************************************************
 * Getters
 **************************************************/
unsigned int
gr_histo_sink_f::get_frame_size(void){
  return d_frame_size;
}

unsigned int
gr_histo_sink_f::get_num_bins(void){
  return d_num_bins;
}

/**************************************************
 * Setters
 **************************************************/
void
gr_histo_sink_f::set_frame_size(unsigned int frame_size){
  gruel::scoped_lock guard(d_mutex);	// hold mutex for duration of this function
  d_frame_size = frame_size;
  /* allocate a new sample array */
  delete [] d_samps;
  d_samps = new float[d_frame_size];
  clear();
}

void
gr_histo_sink_f::set_num_bins(unsigned int num_bins){
  gruel::scoped_lock guard(d_mutex);	// hold mutex for duration of this function
  d_num_bins = num_bins;
  /* allocate a new bin array */
  delete [] d_bins;
  d_bins = new unsigned int[d_num_bins];
  clear();
}
