#pragma once
#ifndef ASSETSTORE_H
#define ASSETSTORE_H

#include<map>
#include<string>
#include<vector>
#include<SDL.h>
#include<SDL_ttf.h>

class AssetStore {
private:
	std::map<std::string, SDL_Texture*>textures;
	std::map<std::string, TTF_Font*>fonts;



public:
	AssetStore();
	~AssetStore();

    void ClearAssets();
    bool HasTexture(const std::string& id) const { auto i = textures.find(id); return i != textures.end() && i->second; }
    bool HasFont(const std::string& id) const { auto i = fonts.find(id); return i != fonts.end() && i->second; }
    std::vector<std::string> GetTextureIds() const;
    std::vector<std::string> GetFontIds() const;
	void AddTexture(SDL_Renderer* renderer, const std::string& assetId, const std::string& filePath);
	SDL_Texture* GetTexture(const std::string& assetId);

	void AddFont(const std::string& assetId, const std::string& filePath, int fontSize);
	TTF_Font* GetFont(const std::string& assetId);

};
#endif // !ASSETSTORE_H
