#include <Arduino.h>
#include <Regexp.h>

#include <iostream>

#include "AsyncDuplex.h"

AsyncDuplex::Command::Command() {}

AsyncDuplex::Command::Command(
    const char* _cmd,
    const char* _expect,
    std::function<void(MatchState)> _success,
    std::function<void()> _failure,
    uint16_t _timeout,
    uint32_t _delay
) {
    strcpy(command, _cmd);
    strcpy(expectation, _expect);
    success = _success;
    failure = _failure;
    timeout = _timeout;
    delay = _delay;
}

AsyncDuplex::AsyncDuplex(){
}

void AsyncDuplex::begin(Stream* _stream) {
    stream = _stream;
    began = true;
}

bool AsyncDuplex::asyncExecute(
    const char *_command,
    const char *_expectation,
    AsyncDuplex::Timing _timing,
    std::function<void(MatchState)> _success,
    std::function<void()> _failure,
    uint16_t _timeout,
    uint32_t _delay
) {
    uint8_t position = 0;
    if(_timing == ANY) {
        position = queueLength;
        queueLength++;
    } else {
        shiftRight();
    }

    strcpy(commandQueue[position].command, _command);
    strcpy(commandQueue[position].expectation, _expectation);
    commandQueue[position].success = _success;
    commandQueue[position].failure = _failure;
    commandQueue[position].timeout = _timeout;

    // Once queued, the delay signifies the point in time at
    // which this task can begin being processed
    commandQueue[position].delay = _delay + millis();

    return true;
}

bool AsyncDuplex::asyncExecute(
    const Command* cmd,
    Timing _timing
) {
    return AsyncDuplex::asyncExecute(
        cmd->command,
        cmd->expectation,
        _timing,
        cmd->success,
        cmd->failure,
        cmd->timeout
    );
}

bool AsyncDuplex::asyncExecuteChain(
    Command* cmdArray,
    uint16_t count,
    Timing _timing
) {
    if(count < 2) {
        return false;
    }

    Command scratch;
    Command chain = cmdArray[count - 1];

    for(int16_t i = count - 2; i >= 0; i--) {
        AsyncDuplex::copyCommand(
            &scratch,
            &cmdArray[i]
        );
        AsyncDuplex::createChain(
            &scratch,
            &chain
        );
        AsyncDuplex::copyCommand(
            &chain,
            &scratch
        );
    }
    AsyncDuplex::asyncExecute(
        &chain,
        _timing
    );
}

void AsyncDuplex::createChain(Command* dest, const Command* toChain) {
    Command chained;
    copyCommand(&chained, toChain);

    std::function<void(MatchState)> originalSuccess = dest->success;
    dest->success = [this, chained, originalSuccess](MatchState ms){
        if(originalSuccess) {
            #ifdef ASYNC_DUPLEX_DEBUG
                std::cout << "Executing pre-chain success fn\n";
            #endif
            originalSuccess(ms);
        }
        else {
            #ifdef ASYNC_DUPLEX_DEBUG
                std::cout << "No pre-chain success fn to execute\n";
            #endif
        }
        #ifdef ASYNC_DUPLEX_DEBUG
            std::cout << "Queueing chained command: ";
            std::cout << chained.command;
            std::cout << "\n";
        #endif
        AsyncDuplex::asyncExecute(
            &chained,
            Timing::NEXT
        );
    };
}

void AsyncDuplex::copyCommand(Command* dest, const Command* src) {
    strcpy(dest->command, src->command);
    strcpy(dest->expectation, src->expectation);
    dest->success = src->success;
    dest->failure = src->failure;
    dest->timeout = src->timeout;
    dest->delay = src->delay;
}

void AsyncDuplex::loop(){
    if(!began) {
        return;
    }
    if(processing && timeout < millis()) {
        std::function<void()> fn = commandQueue[0].failure;
        if(fn) {
            fn();
        }
        shiftLeft();
        inputBuffer[0] = '\0';
        processing=false;
        #ifdef ASYNC_DUPLEX_DEBUG
            std::cout << "Command timeout\n";
        #endif
    }
    while(stream->available()) {
        inputBuffer[bufferPos++] = stream->read();
        inputBuffer[bufferPos] = '\0';

        #ifdef ASYNC_DUPLEX_DEBUG
            std::cout << "Input buffer (";
            std::cout << String(bufferPos);
            std::cout << ") \"";
            std::cout << inputBuffer;
            std::cout << "\"\n";
        #endif

        if(processing) {
            MatchState ms;
            ms.Target(inputBuffer);

            char result = ms.Match(commandQueue[0].expectation);
            if(result) {
                #ifdef ASYNC_DUPLEX_DEBUG
                    std::cout << "Expectation matched\n";
                #endif

                processing=false;

                std::function<void(MatchState)> fn = commandQueue[0].success;
                shiftLeft();
                if(fn) {
                    fn(ms);
                }
                uint16_t offset = ms.MatchStart + ms.MatchLength;
                for(uint16_t i = offset; i < INPUT_BUFFER_LENGTH; i++) {
                    inputBuffer[i - offset] = inputBuffer[i];
                    bufferPos = i - offset;
                    // If we reached the end of the capture, we
                    // do not need to copy anything further
                    if(inputBuffer[i] == '\0') {
                        break;
                    }
                }
            }
        }
    }
    if(!processing && queueLength > 0 && commandQueue[0].delay <= millis()) {
        #ifdef ASYNC_DUPLEX_DEBUG
            std::cout << "Command started: ";
            std::cout << commandQueue[0].command;
            std::cout << "\n";
        #endif
        stream->println(commandQueue[0].command);
        processing = true;
        timeout = millis() + commandQueue[0].timeout;
    }
}

uint8_t AsyncDuplex::getQueueLength() {
    return queueLength;
}

void AsyncDuplex::getResponse(char* buffer, uint16_t length) {
    strncpy(buffer, inputBuffer, length);
}

void AsyncDuplex::shiftRight() {
    for(int8_t i = 0; i < queueLength - 1; i++) {
        AsyncDuplex::copyCommand(&commandQueue[i+1], &commandQueue[i]);
    }
    queueLength++;
}

void AsyncDuplex::shiftLeft() {
    for(int8_t i = queueLength - 1; i > 0; i--) {
        AsyncDuplex::copyCommand(&commandQueue[i-1], &commandQueue[i]);
    }
    queueLength--;
}

inline int AsyncDuplex::available() {
    return stream->available();
}

inline size_t AsyncDuplex::write(uint8_t bt) {
    return stream->write(bt);
}

inline int AsyncDuplex::read() {
    return stream->read();
}

inline int AsyncDuplex::peek() {
    return stream->peek();
}

inline void AsyncDuplex::flush() {
    return stream->flush();
}
