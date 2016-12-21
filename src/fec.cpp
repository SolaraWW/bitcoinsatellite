// Copyright (c) 2016 Matt Corallo
// Unlike the rest of Bitcoin Core, this file is
// distributed under the Affero General Public License (AGPL v3)

#include "fec.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

#define DIV_CEIL(a, b) (((a) + (b) - 1) / (b))

FECDecoder::FECDecoder(size_t data_size, size_t chunks_provided, int32_t prng_seed) :
        chunk_count(DIV_CEIL(data_size, FEC_CHUNK_SIZE)), chunks_recvd(0),
        chunks_sent(chunks_provided), decodeComplete(false),
        chunk_recvd_flags(chunks_sent) {
    if (chunk_count < 2)
        return;
    state = wh256_decoder_init(NULL, data_size, FEC_CHUNK_SIZE);
    assert(state);
}

FECDecoder& FECDecoder::operator=(FECDecoder&& decoder) {
    chunk_count    = decoder.chunk_count;
    chunks_recvd   = decoder.chunks_recvd;
    chunks_sent    = decoder.chunks_sent;
    decodeComplete = decoder.decodeComplete;
    chunk_recvd_flags = std::move(decoder.chunk_recvd_flags);
    memcpy(&tmp_chunk, &decoder.tmp_chunk, sizeof(tmp_chunk));
    state          = decoder.state;
    decoder.state  = NULL;
    return *this;
}

FECDecoder::~FECDecoder() {
    if (state)
        wh256_free(state);
}

bool FECDecoder::ProvideChunk(const unsigned char* chunk, size_t chunk_id) {
    assert(chunk_id < chunks_sent);

    if (decodeComplete)
        return true;

    if (chunk_recvd_flags[chunk_id]) // wirehair breaks if we call it twice with the same packet
        return true;

    chunk_recvd_flags[chunk_id] = true;
    chunks_recvd++;
    if (chunk_count < 2) { // For 1-packet data, just send it repeatedly...
        memcpy(&tmp_chunk, chunk, FEC_CHUNK_SIZE);
        decodeComplete = true;
    } else if (!wh256_decoder_read(state, chunk_id, chunk))
        decodeComplete = true;

    return true;
}

bool FECDecoder::HasChunk(size_t chunk_id) {
    assert(chunk_id < chunks_sent);

    return decodeComplete || chunk_recvd_flags[chunk_id];
}

bool FECDecoder::DecodeReady() const {
    return decodeComplete;
}

const void* FECDecoder::GetDataPtr(size_t chunk_id) {
    assert(DecodeReady());
    assert(chunk_id < chunk_count);
    if (chunk_count >= 2)
        assert(!wh256_decoder_reconstruct_block(state, chunk_id, (void*)&tmp_chunk));
    return &tmp_chunk;
}


FECEncoder::FECEncoder(const std::vector<unsigned char>* dataIn, const int32_t prng_seed, std::vector<unsigned char>* fec_chunksIn)
        : data(dataIn), fec_chunks(fec_chunksIn) {
    assert(fec_chunks->size() % FEC_CHUNK_SIZE == 0);
    assert(!fec_chunks->empty());
    assert(!data->empty());

    if (DIV_CEIL(data->size(), FEC_CHUNK_SIZE) < 2)
        return;

    state = wh256_encoder_init(NULL, data->data(), data->size(), FEC_CHUNK_SIZE);
    assert(state);
}

FECEncoder::~FECEncoder() {
    if (state)
        wh256_free(state);
}

bool FECEncoder::BuildChunk(size_t fec_chunk_id) {
    assert(fec_chunk_id < fec_chunks->size() / FEC_CHUNK_SIZE);

    if (DIV_CEIL(data->size(), FEC_CHUNK_SIZE) < 2) { // For 1-packet data, just send it repeatedly...
        memcpy(&(*fec_chunks)[fec_chunk_id * FEC_CHUNK_SIZE], &(*data)[0], data->size());
        memset(&(*fec_chunks)[fec_chunk_id * FEC_CHUNK_SIZE + data->size()], 0, FEC_CHUNK_SIZE - data->size());
        return true;
    }

    size_t chunk_id = fec_chunk_id + DIV_CEIL(data->size(), FEC_CHUNK_SIZE);
    int chunk_bytes;
    if (wh256_encoder_write(state, chunk_id, &(*fec_chunks)[fec_chunk_id * FEC_CHUNK_SIZE], &chunk_bytes))
        return false;

    if (chunk_bytes != FEC_CHUNK_SIZE)
        memset(&(*fec_chunks)[fec_chunk_id * FEC_CHUNK_SIZE + chunk_bytes], 0, FEC_CHUNK_SIZE - chunk_bytes);

    return true;
}

bool FECEncoder::PrefillChunks() {
    return BuildChunk((fec_chunks->size() / FEC_CHUNK_SIZE) - 1);
}

bool BuildFECChunks(const std::vector<unsigned char>& data, const int32_t prng_seed, std::vector<unsigned char>& fec_chunks) {
    FECEncoder enc(&data, prng_seed, &fec_chunks);
    for (size_t i = 0; i < fec_chunks.size() / FEC_CHUNK_SIZE; i++)
        if (!enc.BuildChunk(i))
            return false;
    return true;
}

class FECInit
{
public:
    FECInit() {
        assert(!wirehair_init());
    }
} instance_of_fecinit;