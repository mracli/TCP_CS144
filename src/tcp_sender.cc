#include "tcp_sender.hh"
#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_sender_message.hh"
#include "wrapping_integers.hh"
#include <cstdint>
#include <optional>

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  // Your code here.
  return seq_no_in_flight_cnt_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  // Your code here.
  return retransmission_cnt_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // Your code here.
  auto const cur_window_size = window_size_ ? window_size_ : 1u;
  while ( cur_window_size >= sequence_numbers_in_flight() ) {
    auto const available_window = cur_window_size - sequence_numbers_in_flight();
    auto msg = make_empty_message();
    if(msg.RST){
      transmit(msg);
      break;
    }
    if ( !syn_ )
      syn_ = msg.SYN = true;

    auto const payload_size = min(min(
      TCPConfig::MAX_PAYLOAD_SIZE, 
      available_window - msg.sequence_length()),
      reader().bytes_buffered());
    read( input_.reader(), payload_size, msg.payload );

    if ( !fin_ && reader().is_finished() && available_window > msg.sequence_length() )
      fin_ = msg.FIN = true;

    // when sequence_length == 0, which is meaningless message
    if (msg.sequence_length() == 0) break;

    // special case: FIN message cannot exceed receiver's window
    if ( msg.FIN && available_window < msg.sequence_length() )
      fin_ = msg.FIN = false;
    
    const auto size = msg.sequence_length();

    seq_no_in_flight_cnt_ += size;
    next_seq_no_ += size;
    outstanding_segments_.push( msg );
    transmit( msg );
    if ( !timer_.is_running() ) {
      timer_.start();
    }
  }
  if(outstanding_segments_.empty())
    timer_.stop();
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  // Your code here.
  return { Wrap32::wrap( next_seq_no_, isn_ ), {}, {}, {}, has_error()};
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // Your code here.
  window_size_ = msg.window_size;

  if ( msg.RST ) {
    set_error();
    timer_.stop();
  }
  // error state, don't do any thing!
  if ( has_error() )
    return;
  // no any ackno is meaningless
  if ( !msg.ackno.has_value() )
    return;

  auto const received_abs_ack_no = msg.ackno->unwrap( isn_, next_seq_no_ );
  if ( received_abs_ack_no > next_seq_no_ )
    return;

  // Now we got the newest msg to update current state
  // check if any outstanding segments already received.
  bool success = false;
  while ( outstanding_segments_.size() ) {
    auto& front_seg = outstanding_segments_.front();
    // front seg still not sent to peer successfully
    if ( front_seg.seqno.unwrap( isn_, next_seq_no_ ) + front_seg.sequence_length() > received_abs_ack_no )
      break;
    seq_no_in_flight_cnt_ -= front_seg.sequence_length();
    outstanding_segments_.pop();
    success = true;
  }

  if ( success ) {
    timer_.reset_RTO();
    retransmission_cnt_ = 0;
    timer_.start();
  }

  if ( outstanding_segments_.empty() )
    timer_.stop();
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  // Your code here.
  // (void)ms_since_last_tick;
  // (void)transmit;
  // (void)initial_RTO_ms_;
  timer_.tick( ms_since_last_tick );
  // send the oldest outstanding segment when timer is expired
  if ( timer_.is_expired() ) {
    transmit( outstanding_segments_.front() );
    if ( window_size_ != 0 ) {
      ++retransmission_cnt_;
      timer_.double_RTO();
    }
    timer_.start();
  }
}