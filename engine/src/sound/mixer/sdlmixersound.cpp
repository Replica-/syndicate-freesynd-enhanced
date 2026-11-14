/*
 *  FreeSynd - a remake of the classic Bullfrog game "Syndicate".
 *
 *   Copyright (C) 2005  Stuart Binge  <skbinge@gmail.com>
 *   Copyright (C) 2005  Joost Peters  <joostp@users.sourceforge.net>
 *   Copyright (C) 2006  Trent Waddington <qg@biodome.org>
 *   Copyright (C) 2006  Tarjei Knapstad <tarjei.knapstad@gmail.com>
 *   Copyright (C) 2024-2025  Benoit Blancard <benblan@users.sourceforge.net>
 *
 *   This program is free software: you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as 
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>. 
 * 
 */

#include "sdlmixersound.h"

#include <cstring>
#include <vector>

#include "fs-utils/log/log.h"

namespace {

const char k_VocSignature[] = "Creative Voice File\x1a";
const uint32_t k_VocSignatureLen = 20;

/*!
 * Decodes a Creative Voice file (the format of the original game samples)
 * and converts it to the format of the audio device.
 * SDL_mixer's own VOC loader resamples with SDL_ConvertAudio, which can write
 * out of bounds for these odd sample rates and crash, so an SDL_AudioStream is
 * used instead.
 * \param data The VOC file
 * \param size The size of the VOC file
 * \return The new chunk or NULL if the data could not be decoded.
 */
Mix_Chunk *loadVoc(const uint8_t *data, uint32_t size) {
    int rate = 0;
    std::vector<uint8_t> pcm;

    uint32_t pos = data[20] | (data[21] << 8);
    while (pos + 4 <= size && data[pos] != 0) {
        uint8_t type = data[pos];
        uint32_t len = data[pos + 1] | (data[pos + 2] << 8) | (data[pos + 3] << 16);
        pos += 4;
        if (len > size - pos) {
            len = size - pos;
        }

        if (type == 1 && len >= 2) {
            // Sound data : time constant, pack method then samples.
            // Only unpacked 8 bits samples are supported
            if (data[pos + 1] != 0) {
                return NULL;
            }
            rate = 1000000 / (256 - data[pos]);
            pcm.insert(pcm.end(), data + pos + 2, data + pos + len);
        } else if (type == 2) {
            // Sound continue
            pcm.insert(pcm.end(), data + pos, data + pos + len);
        }
        pos += len;
    }

    int frequency;
    int channels;
    Uint16 format;
    if (rate == 0 || pcm.empty() || !Mix_QuerySpec(&frequency, &format, &channels)) {
        return NULL;
    }

    SDL_AudioStream *stream = SDL_NewAudioStream(AUDIO_U8, 1, rate, format, channels, frequency);
    if (!stream) {
        return NULL;
    }

    Mix_Chunk *chunk = NULL;
    if (SDL_AudioStreamPut(stream, pcm.data(), static_cast<int>(pcm.size())) == 0 &&
            SDL_AudioStreamFlush(stream) == 0) {
        int available = SDL_AudioStreamAvailable(stream);
        Uint8 *buffer = static_cast<Uint8 *>(SDL_malloc(available));
        int len = buffer ? SDL_AudioStreamGet(stream, buffer, available) : -1;
        if (len > 0) {
            // Mix_FreeChunk() frees the chunk and its buffer with SDL_free()
            chunk = static_cast<Mix_Chunk *>(SDL_malloc(sizeof(Mix_Chunk)));
        }
        if (chunk) {
            int frameSize = (SDL_AUDIO_BITSIZE(format) / 8) * channels;
            chunk->allocated = 1;
            chunk->abuf = buffer;
            chunk->alen = len - (len % frameSize);
            chunk->volume = MIX_MAX_VOLUME;
        } else {
            SDL_free(buffer);
        }
    }

    SDL_FreeAudioStream(stream);
    return chunk;
}

}

namespace fs_eng {

/*!
 * Class constructor. Initialize date with NULL.
 */
SdlMixerSound::SdlMixerSound():sound_data_(NULL)
{
}

/*!
 * Class destructor. free the sound data if it
 * was allocated.
 */
SdlMixerSound::~SdlMixerSound()
{
    if (sound_data_) {
        Mix_FreeChunk(sound_data_);
    }
}

/*!
 * Plays the sound a number of times.
 * The method checks if the sound system has been initialized before
 * playing the sound.
 * \param loops The number of times the sound is played. Value of -1
 * plays the sound indefinitly until the sound is stopped.
 * \param channel
 */
void SdlMixerSound::play(int channel, int loops) const
{
    int ret = Mix_PlayChannel(channel, sound_data_, loops);
    if (ret < 0) {
        FSERR(Log::k_FLG_SND, "SdlMixerSound", "play", ("Failed to play sound on channel 0."));
    }
}

/*!
 * Stops the sound from playing.
 * \param channel
 */
void SdlMixerSound::stop(int channel) const
{
    Mix_HaltChannel(channel);
}

/*!
 * Each sample has its own volume wich is taken into account
 * on the mixing phase. This method sets the volume of this
 * sample.
 * \param A value between 0 and maximum volume.
 * \return true if the volume has been correctly set.
 */
bool SdlMixerSound::setVolume(int volume)
{
    int ret = Mix_VolumeChunk(sound_data_, volume);
    if (ret < 0) {
        FSERR(Log::k_FLG_SND, "SdlMixerSound", "setVolume", ("Failed setting volume on Sound."));
        return false;
    }

    return true;
}

/*!
 * Loads the sample from the given data.
 * \param soundData Data as loaded from original resource
 * \param size The size of the input data
 */
bool SdlMixerSound::loadSound(uint8_t * soundData, uint32_t size)
{
    Mix_Chunk *newsound;
    if (size > k_VocSignatureLen && memcmp(soundData, k_VocSignature, k_VocSignatureLen) == 0) {
        newsound = loadVoc(soundData, size);
    } else {
        SDL_RWops *rw = SDL_RWFromMem(soundData, size);
        if (!rw) {
            FSERR(Log::k_FLG_SND, "SdlMixerSound", "loadSound", ("Failed creating SDL_RW buffer from memory"));
            return false;
        }
        newsound = Mix_LoadWAV_RW(rw, 1);
    }

    if (!newsound) {
        FSERR(Log::k_FLG_SND, "SdlMixerSound", "loadSound", ("Failed loading sound from SDL_RW buffer"));
        return false;
    }
    if (sound_data_ != 0)
        Mix_FreeChunk(sound_data_);
    sound_data_ = newsound;
    return true;
}
}
