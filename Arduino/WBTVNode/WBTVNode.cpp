#include "WBTVNode.h"


#ifdef ADV_MODE
struct WBTV_Time_t WBTVClock_Sys_Time;
/**
 *The current estimate of accumulated error in WBTVs system clock.
 *The error is measured in 2**16ths of a second, but it saturates at 4294967294 and does not roll over.
 *4294967295 is reserved for when the time has never been set.
 */

unsigned long WBTVClock_error = 4294967295; // The accumulated error estimator in seconds/2**16

//Assume an error per second of about 5000PPM, which is about 10 minutes in day,
//Which is approximately what one gets with ceramic resonators.

/**This is how much error you estimate to accumulate per second with the built in crystal.
 * The error is measured in parts per 2**16. The default is 2500 or about 4%
 * This is very conservative and the arduino's crystal is more like 0.1% or better.
 * However some other devices may have much more error.
 * The default is 4% becuase UARTs don't work well with more error than that so it's
 * A safe assumtion.
 */
unsigned int WBTVClock_error_per_second = 2500;

unsigned long WBTVClock_prevMillis = 0;

/*Manually set the WBTV Clock. time must be the current UNIX time number,
 *fraction is the fractional part of the time in 2**16ths of a second,
 *error_ms is the error in 2**16ths of second that you estimate your source of timing to contain.
 *Times entered by humans should always be suspect to be several minutes off.
*/
void WBTVClock_set_time(long long time, unsigned int fraction, unsigned long error)
{
    //If our estimated internal time accuracy is better than whatever this new
    //Time Source alledges, then avoid making the accuracy worse.
    if (error>WBTVClock_error)
    {
        return;
    }
    WBTVClock_prevMillis=millis();
    
    WBTVClock_prevMillis -= fraction >>6;
    //Now we add the fraction value divided by 2**12
    //To compensate for dividing by 64 being too much.
    WBTVClock_prevMillis += fraction >>12;
    WBTVClock_prevMillis += fraction >>13;
    WBTVClock_error = error; 
}

/**
 *Returns the current time as a struct, the integer part called seconds
 *and the 16 bit fraction part called time.
 */
struct WBTV_Time_t WBTVClock_get_time()
{
unsigned long temp;

temp = millis();

while(temp - WBTVClock_prevMillis >= 1000){
    WBTVClock_Sys_Time.seconds++;
    WBTVClock_prevMillis += 1000;
    if (WBTVClock_error<4294900000)
    {
    WBTVClock_error+= WBTVClock_error_per_second;
    }
    else
    {
    //This value reserved for "synchronized but error too high to count
    WBTVClock_error = 4294967294;
    }
  }
  
  //(1000 * 65) + 500 is 2**16 to within a tenth of a percent.
  //When the milliseconds value is 1000, the output will be low by 63
  //The average error is therefore 31.5, and is always low.
  
  //Therefore we must add 32, which in the average case will make it more accurate,
  //And in the worst case will make it less accurate by a very tiny amount.
  
  temp -= WBTVClock_prevMillis;
  WBTVClock_Sys_Time.fraction = ((temp*65)+(temp>>1))+32;
  return (WBTVClock_Sys_Time);
}


#endif

/*
 *Instantiate a wired-OR WBTV node with CSMA, collision avoidance,
 *and collision detection. bus_sense_pin must be the RX pin, and
 *port must be a Serial or other stream object
 */
 WBTVNode::WBTVNode(Stream *port,int bus_sense_pin)
{
  BUS_PORT=port;
  sensepin = bus_sense_pin;
  wiredor = 1;
}

/*
 *This lets you use WBTV over full duplex connections like usb to serial.
 */
WBTVNode::WBTVNode(Stream *port)
{
  BUS_PORT=port;
  wiredor =0;
}

void WBTVNode::dummyCallback(
unsigned char * header, 
unsigned char headerlen, 
unsigned char * data, 
unsigned char datalen)
{
  //Does absoulutly nothing
}


void WBTVNode::setBinaryCallback(
void (*thecallback)(
unsigned char *, 
unsigned char , 
unsigned char *, 
unsigned char ))
{
  callback = thecallback;
}


/*Set a function taking two strings as input to be called when a packet arrives*/
void WBTVNode::setStringCallback( void (*thecallback)( char *,  char *))
{
  stringCallback = thecallback;
  callback = 0;
}

void WBTVNode::stringSendMessage(const char *channel, const char *data)
{
  sendMessage((const unsigned char *)channel,strlen(channel),(const unsigned char *) data,strlen(data));
}


/*Given a channel as pointer to unsigned char, channel length, data, and data length, send it out the serial port.*/
void WBTVNode::sendMessage(const unsigned char * channel, const unsigned char channellen, const unsigned char * data, const unsigned char datalen)
{
  unsigned char i;

  //If at any time an error is found, go back here to retry
waiting:
  //Wait till the bus is free
  if(wiredor)
  {
    waitTillICanSend();
  }
  //Reset the checksum engine
  sumSlow = sumFast =0;

  //Basically, send bytes, if we get interfered with, go back to waiting


#ifdef DUMMY_STH
  //Sent dummy start code for reliabilty in case there was noise that looked like an ESC.
  //This is really just there for paranoia reasons but it's a nice addition and it's only one more byte.
  if (!writeWrapper(STH))
  {
    goto waiting;
  }
#endif

  if (!writeWrapper(STH))//Sent start code
  {
    goto waiting;
  }
  //Send header(escaped)
  for (i = 0; i<channellen;i++)
  {
    updateHash(channel[i]); ///Update the hash with every header byte we send
    if (!escapedWrite(channel[i]))
    {
      goto waiting;

    }
  }

  //Send the start-of-message(end of header)
  if (!writeWrapper(STX))
  {
    goto waiting;
  }

#ifdef HASH_STX
  updateHash(STX);
#endif

  //Send the message(escaped)
  for (i = 0; i<datalen;i++)
  {
    updateHash(data[i]); //Hash the message
    if (!escapedWrite(data[i]))
    {
      goto waiting;

    }
  }

  //Send the two bytes of the checksum, After doing the proper fletcher derivation.
  //When the reciever hashses the message along with the checksum,the result should be 0.
  if (!escapedWrite(sumSlow))

  {
    goto waiting;
  }

  if (!escapedWrite(sumFast))
  {
    goto waiting;
  }

  if (!writeWrapper(EOT))
  {
    goto waiting;
  }


}

//Note that one hash engine gets used for sending and recieving. This works for now because we calc the hash all at once after we are
//done recieving
//i.e. hashing a recieved packet is atomic
void WBTVNode::updateHash(unsigned char chr)
{
  //This is a fletcher-256 hash. Not quite as good as a CRC, but pretty good.
  sumSlow += chr;
  sumFast += sumSlow; 
}

void WBTVNode::service()
{
    if (BUS_PORT->available())
    {
      decodeChar(BUS_PORT->read());
    }
    
lastServiced = millis();

}

//Process one incoming char
void WBTVNode::decodeChar(unsigned char chr)
{
  //Set the garbage flag if we get a message that is too long
  if (recievePointer > MAX_MESSAGE)
  {        
    garbage = 1;
    recievePointer = 0;
  }

  //Handle the special chars
  if (!escape)
  {
    if (chr == STH)
    {
      #ifdef RECORD_TIME
        //Keep track of when the msg started, or else time sync won't work.
        message_start_time = millis();
        
        //If the new byte is the only byte, then it must have arrived
        //At some point between the last time it was polled and now.
        //For best average performance, we assume it arrived at the halfway point.
        //If it is not the only byte it cannon be trusted, so message time
        //accurate must be set to 0
        
        message_start_time -= ((message_start_time-lastServiced)>>1);
        
        message_time_error = message_start_time-lastServiced;
        //If there is another byte in the stream, then consider the arrival time invalid. 
        if (BUS_PORT->available())
        {
            message_time_accurate = 0;
        }
        else
        {
             message_time_accurate = 1;
        }
      #endif
      
      
      //an unescaped start of header byte resets everything. 
      recievePointer = 0;
      headerTerminatorPosition = 0; //We need to set this to zero to recoginze missing data sections, they will look like 0 len headers because this wont move.
      garbage = 0;
      
      #ifdef WBTV_SEED_ARDUINO_RNG
      randomSeed(micros()+random(100000);
      #endif
      
      #ifdef WBTV_ENABLE_RNG
      WBTV_doRand(sumSlow+micros());
      #endif

      return;
    }

    //Handle the division between header and text
    if (chr == STX)
    {
      //If this isn't 0, the default, then that would indicate a message with multiple segments,
      //which simply can't be handled by this library as it, so they are ignored.
      //CHANGE THIS IF YOU WANT TO ADD SUPPORT FOR MULTI-SEGMENT MESSAGES
      if (headerTerminatorPosition)
      {
        garbage =  1;
      }

      headerTerminatorPosition = recievePointer;
      message[recievePointer] = 0; //Null terminator between header and data makes string callbacks work
      recievePointer ++;

      return;

    }

    //Handle end of packet
    if (chr == EOT)
    {
      unsigned char i;
      if (garbage)//If this packet was garbage, throw it away
      {
        return;
      }

      if (!headerTerminatorPosition)
        //If the headerTerminatorPosition is 0, then we either have a zero length header, or we never recieved a end-of header char.
        //I never specifically wrote that 0-len headers are illegal in the spec. but maybe I should. Otherwise this is a bug [ ]
      {
        return;
      }

      //not possible to be a valid message becuse len(checksum) = 2
      if(recievePointer <2)
      {
        return;
      }
      
      //Reset the hash state to calculate a new hash
      sumFast=sumSlow=0;

      //Hash the packet
      for (i=0; i< recievePointer-2;i++)

      {
        //Ugly slow hack to deal with the empty space we put in
        if(! (i==headerTerminatorPosition))
        {
          updateHash((message[i]));
        }

#ifdef HASH_STX
        else
        {
          updateHash('~');
        }
#endif
      }

      //Check the hash
      if ((message[recievePointer-1]== sumFast) & (message[recievePointer-2]== sumSlow))
      {
        #ifdef ADV_MODE
        if (internalProcessMessage())
        {
        return;
        }
        #endif
        message[recievePointer-2] = 0; //Null terminator for people using the string callbacks.
        
        //If there is a callback set up, use it.
        if(callback)
        {
          callback((unsigned char*)message ,
          headerTerminatorPosition,
          (unsigned char *)message+headerTerminatorPosition+1, //The plus one accounts for the null terminator we put in
          recievePointer-(headerTerminatorPosition+1)); //Would be plus 2 for the checksum but we have the null byte inserted so it's plus 1
        }
        else
        {
            //If there are any NUL bytes in the header, just return.
            //Presumably nobody would register a string callback
            //that listens on a channel with 0s in its data
            //but the channel needs to be checked to prevent against
            //channels that start with the name of the string channel
            //and then a null.
            for(i=0;i<headerTerminatorPosition;i++)
            {
                if (message[i] ==0)
                    {
                        return;
                    }
            }
            //Veriied that the channel name is safe. now we hand it off to the callback
            if (stringCallback)
            {
              stringCallback((char*)message ,
              (char *)message+headerTerminatorPosition);
            }
        }
      }
      #ifdef WBTV_SEED_ARDUINO_RNG
      randomSeed(sumSlow+random(100000);
      #endif
      
      #ifdef WBTV_ENABLE_RNG
      WBTV_doRand(sumSlow+micros());
      #endif

      return;
    }
  }

  if ( chr == ESC)
  {
    if (!escape)
    {
      escape = 1;
      return; 
    }
    else
    {
      escape = 0;
      message[recievePointer] = ESC;
      recievePointer ++;
      return;
    }
  }
  else
  {
    escape =0;
  }
  message[recievePointer] = chr;
  recievePointer++;
  

}

unsigned char WBTVNode::writeWrapper(unsigned char chr)
{
  unsigned char x;
  unsigned long start;
  BUS_PORT->read();
  BUS_PORT->write(chr);
  if (wiredor)
  {
    start = millis();
    while (!BUS_PORT->available())
    {
      if ((millis()-start)>MAX_WAIT)
      {
        return 0;
      }
    }


    if (! (BUS_PORT->read()==chr))
    {
      return 0;
    }
    else
    {
      return 1;
    }
  }
  //Not wiredor, always return 1 because there is no way for this to be interfered with
  else 
  {
    return 1;
  }
}

unsigned char WBTVNode::escapedWrite(unsigned char chr)
{
  unsigned char x;

  x = 1;

  //If chr is a special character, escape it first
  if (chr == STH)
  {
    x = writeWrapper(ESC); 
  }
  if (chr == STX)
  {
    x= writeWrapper(ESC); 
  }
  if (chr == EOT)
  {
    x = writeWrapper(ESC); 
  }
  if (chr == ESC)
  {
    x =writeWrapper(ESC); 
  }

  //If the escape succeded(or there was no escape), then send the char, and return it's sucess value
  if (x)
  {
    return writeWrapper(chr);
  }
  //Else return a failure
  return 0;

}

//Block until the bus is free.
//It does this by waiting a random time and making sure there are no transitions in that period
//the collision detection and immediate retry means that
//even a relatively high rate of collisions should not cause problems.

//A cool side effect of this is that in many cases nodes will be able to "wait out" noise on the bus
void WBTVNode::waitTillICanSend()
{
  unsigned long start,time;
wait:
  start = micros();
  #ifdef WBTV_ENABLE_RNG
  time = WBTV_rand(MIN_BACKOFF,MAX_BACKOFF);
  #else
  time = random(MIN_BACKOFF , MAX_BACKOFF);
  #endif
  

  //While it has been less than the required time, just loop. Should the bus get un-idled in that time, totally restart.
  // The performance of the loop is really pooptastic, because  digital reads take 1us of so and there is a divide operation in the 
  //micros() 
  //implementation. Still, this should arbitrate well enough.
  while( (micros()-start) < time)
  {
    //UNROLL LOOP X4 TO INCREASE CHACE OF CATCHING FAST PULSES
    //We directly read from the pin to determine if it is idle, that lets us catch the 
    if (!(digitalRead(sensepin)== BUS_IDLE_STATE))
    {
      goto wait;
    }
    //We directly read from the pin to determine if it is idle, that lets us catch the 
    if (!(digitalRead(sensepin)== BUS_IDLE_STATE))
    {
      goto wait;
    }
    //We directly read from the pin to determine if it is idle, that lets us catch the 
    if (!(digitalRead(sensepin)== BUS_IDLE_STATE))
    {
      goto wait;
    }
    //We directly read from the pin to determine if it is idle, that lets us catch the 
    if (!(digitalRead(sensepin)== BUS_IDLE_STATE))
    {
      goto wait;
    }
  }
}

#ifdef ADV_MODE
unsigned char WBTVNode::internalProcessMessage()
{
    unsigned long error_temp;
    if(headerTerminatorPosition == 4)
    {
    if (memcmp(message,"TIME",4)==0)

    {
        //If the exponent is bigger than 8 we can't store that big of number
        //So assume the error is too high to count and store the flag value.
        if ((*((signed char*) (message+17) ) ) > 8)
        {
            error_temp = (4294967294);
        }
        else
        {
        
            //Calculate error based on the floating point value given
            //In the time broadcast and the error we get from the poll rate.
            
            //If the error exponent is less than -16, that would require shifting
            //in the other direction. Instead of actually estimating that,
            //lets assume the error is the maximum you can represent with an exponent of -16
            //In the worst case this will make our estimate 4ms too high.
            if ((*(signed char*) (message+17)) >-16)
            {
                error_temp = *(unsigned char*) (message+18);
                
                //The fixed point value we are going for is measured in 2**16ths of a second
                //therefore, if a the exponent is -16, the multiplier and the output are
                //one and the same.
                error_temp = error_temp << ((*(signed char*) (message+17))+15);
            }
            else
            {
                error_temp=255;
            }
            //Message_time_error is in milliseconds, so we multiply by 64
            //to get approximate 2*16ths of a second.
            //Then we add msgtimeerror*2, to improve the aprroximation and make it conservative.
            //
            error_temp += ((unsigned long) message_time_error)<<6;
            error_temp += ((unsigned long) message_time_error)<<1;
            
            if (!(message_time_accurate))
            {
                //Message time accurate is true when the message arrival time
                //Is known to within a small range because the start byte was the only
                //byte there.
                //That means it must not have been there last time it was polled(or it
                //would have been processed), and s must have arrived between the last two polling
                //cycles. This means it could have arrived at any time at all.
                //So we tack on up to 5 minutes of extra error in that case.
                if (error_temp <4275306493)
                {
                   error_temp+= 19660800;
                }
                else
                {
                   error_temp = (4294967294);
                }
                
            }

    }
        
        //If the estimated accuracy of WBTV's internal clock is better than
        //The estimated accuracy of the new time value, ignore the new value
        //And keep the current estimate.
        if(error_temp <=  WBTVClock_error)
        {
            
            WBTVClock_prevMillis=message_start_time;
            WBTVClock_error = error_temp;
            //5 Accounts for the 5 bytes of
            WBTVClock_Sys_Time.seconds = *(long long*) (message+5);
            //Message+ten= 8 to skip over the seconds part, and 2 to get to the 2 most significant bytes
            //of the fractional portion(Because the WBTV time spec uses 32 bit fractions but this
            //Library only uses 16 bits for the fractional time.
            
            //This is basically approximate division by 65.53 to map
            //The fractional seconds to milliseconds.
            
            //We subtract this milliseconds value from prevMillis to
            //Attempt to make it equal to the millis value
            //exactly when the second rolled over.
            WBTVClock_prevMillis -= (*(unsigned int*) (message+15)) >>6;
            //Now we add the fraction value divided by 2**12
            //To compensate for dividing by 64 being too much.
            WBTVClock_prevMillis += (*(unsigned int*) (message+15)) >>12;
            WBTVClock_prevMillis += (*(unsigned int*) (message+15)) >>13;


            //15 not 13 because we only want the two most significant bytes because
            //arduino doesnt have ghz resolution
            

        }
        

    return (1);
}
}
return(0);
}
void WBTVNode::sendTime()
{
    unsigned char i;
    unsigned long temp;
    signed char count;
    struct WBTV_Time_t t;
    start:
    sumFast=sumSlow =0;
    waitTillICanSend();
    if (writeWrapper('!'))
        {
            t = WBTVClock_get_time();
        }
    else
    {
        goto  start;
    }
    
    if(!writeWrapper('T'))
       {
        goto start;
       }
     if(!writeWrapper('I'))
       {
        goto start;
       }
        if(!writeWrapper('M'))
       {
        goto start;
       }
        if(!writeWrapper('E'))
       {
        goto start;
       }
       updateHash('T');updateHash('I');updateHash('M');updateHash('E');

    if(!writeWrapper('~'))
       {
        goto start;
       }
       #ifdef HASH_STX
       updateHash('~');
       #endif
       
    for (i=0;i<8;i++)
    {
        if(!escapedWrite(((const unsigned char *)(& t.seconds))[i]))
        {
            goto start;
        }
        updateHash(((const unsigned char *)(& t.seconds))[i]);
    }
    
    //We don't know what the two least significant bytes of the 32 bit fraction are.
    //So we just send 0.5 times the possible range.
    if(!escapedWrite(0))
        {
            goto start;
        }
        updateHash(0);
        
    if(!escapedWrite(127))
        {
            goto start;
        }
        updateHash(127);
        
    
    for (i=0;i<2;i++)
    {
        if(!escapedWrite(((unsigned char *)(& t.fraction))[i]))
        {
            goto start;
        }
        updateHash(((unsigned char *)(& t.fraction))[i]);
    }
    
    //If the error is too high to count, assume that it could be any crazy insane number.
    //Like perhaps older than the earth....
    if(WBTVClock_error >= 4294967294)
    {
      
        if(!escapedWrite(127))
        {
            goto start;
        }
        updateHash(127);
        
        if(!escapedWrite(255))
        {
            goto start;
        }
        updateHash(255);
    }
    
    else
    {
        temp = WBTVClock_error;
        count =-15;
        
        while (temp& (~(unsigned long)0xff))
        {
            count ++;
            temp = temp>>1;
            
        }
    
    if(!escapedWrite(count))
        {
            goto start;
        }
        updateHash(count);
        
    if(!escapedWrite(temp&0xff))
        {
            goto start;
        }
        updateHash(temp&0xff);
    }
        
        if(!escapedWrite(sumSlow))
        {
            goto start;
        }
        if(!escapedWrite(sumFast))
        {
            goto start;
        }
        
        if(!writeWrapper('\n'))
        {
            goto start;
        }
}

#endif






