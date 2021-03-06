// Copyright 2019 Xsolla Inc. All Rights Reserved.
// @author Vladimir Alyamkin <ufna@ufna.ru>

#pragma once

#include "XsollaLoginTypes.h"

#include "Blueprint/UserWidget.h"
#include "Http.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/SubsystemCollection.h"

#include "XsollaLoginSubsystem.generated.h"

/** Verb (GET, PUT, POST) used by the request */
UENUM(BlueprintType)
enum class EXsollaLoginRequestVerb : uint8
{
	GET,
	POST,
	PUT,
	DELETE
};

class FJsonObject;

/** Common callback for operations without any user-friendly messages from server on success */
DECLARE_DYNAMIC_DELEGATE(FOnRequestSuccess);

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnAuthUpdate, const FXsollaLoginData&, LoginData);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnSocialUrlReceived, const FString&, Url);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnAuthError, const FString&, Code, const FString&, Description);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnCodeReceived, const FString&, Code);

UCLASS()
class XSOLLALOGIN_API UXsollaLoginSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UXsollaLoginSubsystem();

	// Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem

	/** Initialize controller with provided project and login id (use to override project settings)
	 *
	 * @param InProjectId New project id value.
	 * @param InLoginId New login project id value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	void Initialize(const FString& InProjectId, const FString& InLoginId);

	/**
	 * Adds a new user to the database. The user will receive an account confirmation message to the specified email.
	 *
	 * @param Username Username. Required.
	 * @param Password Password. Required.
	 * @param Email Email. Required.
	 * @param SuccessCallback Callback function called after successful user registration. Account confirmation message will be send to specified email.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "SuccessCallback, ErrorCallback"))
	void RegistrateUser(const FString& Username, const FString& Password, const FString& Email, const FOnRequestSuccess& SuccessCallback, const FOnAuthError& ErrorCallback);

	/**
	 * Authenticates the user by the username and password specified.
	 *
	 * @param Username Username. Required.
	 * @param Password Password. Required.
	 * @param SuccessCallback Callback function called after successful user authentication. Authentication data including JWT will be received.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 * @param bRememberMe Whether the user agrees to save the authentication data. Default is 'false'.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "SuccessCallback, ErrorCallback"))
	void AuthenticateUser(const FString& Username, const FString& Password, const FOnAuthUpdate& SuccessCallback, const FOnAuthError& ErrorCallback, bool bRememberMe = false);

	/**
	 * Resets the user's password.
	 *
	 * @param Username Username. Required.
	 * @param SuccessCallback Callback function called after successful user password reset.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "SuccessCallback, ErrorCallback"))
	void ResetUserPassword(const FString& Username, const FOnRequestSuccess& SuccessCallback, const FOnAuthError& ErrorCallback);

	/** Internal request for token validation (called with each auth update automatically)
	 *
	 * @param SuccessCallback Callback function called after successful token validation.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "SuccessCallback, ErrorCallback"))
	void ValidateToken(const FOnAuthUpdate& SuccessCallback, const FOnAuthError& ErrorCallback);

	/** Get URL for authentication via specified social network
	 *
	 * @param ProviderName Name of social network. Provider must be enabled for the project. Required.
	 * @param SuccessCallback Callback function called after URL for social authentication was successfully received.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "SuccessCallback, ErrorCallback"))
	void GetSocialAuthenticationUrl(const FString& ProviderName, const FOnSocialUrlReceived& SuccessCallback, const FOnAuthError& ErrorCallback);

	/** Open social authentication URL in browser
	 *
	 * @param SocialAuthenticationUrl URL with social network authentication form.
	 * @param BrowserWidget Widget to represent social network authentication form. Can be set in project settings.
	 * @param bRememberMe Whether the user agrees to save the authentication data. Default is 'false'.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	void LaunchSocialAuthentication(const FString& SocialAuthenticationUrl, UUserWidget*& BrowserWidget, bool bRememberMe = false);

	/** Set new value of token (used when token obtained via social network authentication etc.)
	 *
	 * @param Token User authorization token.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	void SetToken(const FString& Token);

	/** Authenticates user by exchanging platform specific session ticket to token
	 *
	 * @param ProviderName Platform on which session ticket was obtained.
	 * @param SessionTicket Session ticket.
	 * @param AppId Platform application identifier.
	 * @param SuccessCallback Callback function called after successful user authentication with platform session ticket. Authentication data including JWT will be received.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	void AuthenticateWithSessionTicket(const FString& ProviderName, const FString& SessionTicket, const FString& AppId, const FOnAuthUpdate& SuccessCallback, const FOnAuthError& ErrorCallback);

	/** Update list of user attributes (cached locally)
	 *
	 * @param AuthToken User authorization token.
	 * @param UserId Identifier of user for which attributes should be updated.
	 * @param AttributeKeys Keys of attributes that should be updated.
	 * @param SuccessCallback Callback function called after successful user attributes local cache update.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "AttributeKeys, SuccessCallback, ErrorCallback"))
	void UpdateUserAttributes(const FString& AuthToken, const FString& UserId, const TArray<FString>& AttributeKeys, const FOnRequestSuccess& SuccessCallback, const FOnAuthError& ErrorCallback);

	/** Modify list of user attributes by creating/editing its items (changes made on server side).
	 *
	 * @param AuthToken User authorization token.
	 * @param AttributesToModify List of new/edited attributes.
	 * @param SuccessCallback Callback function called after successful user attributes modification on server side.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "SuccessCallback, ErrorCallback"))
	void ModifyUserAttributes(const FString& AuthToken, const TArray<FXsollaUserAttribute>& AttributesToModify, const FOnRequestSuccess& SuccessCallback, const FOnAuthError& ErrorCallback);

	/** Remove user attributes with specified keys (changes made on server side).
	 *
	 * @param AuthToken User authorization token.
	 * @param AttributesToRemove List of attribute keys to be removed.
	 * @param SuccessCallback Callback function called after successful user attributes removal on server side.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "SuccessCallback, ErrorCallback"))
	void RemoveUserAttributes(const FString& AuthToken, const TArray<FString>& AttributesToRemove, const FOnRequestSuccess& SuccessCallback, const FOnAuthError& ErrorCallback);

	/** Creates code for linking user platform account to main account
	 *
	 * @param AuthToken User authorization token.
	 * @param SuccessCallback Callback function called after successful accout linking code creation. New linking code will be received.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "SuccessCallback, ErrorCallback"))
	void CreateAccountLinkingCode(const FString& AuthToken, const FOnCodeReceived& SuccessCallback, const FOnAuthError& ErrorCallback);

	/** Links user platform account to main account
	 *
	 * @param UserId Identifier of platform account user.
	 * @param Platform Platform name.
	 * @param Code Account linking code obtained from master account.
	 * @param SuccessCallback Callback function called after successful account linking.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "SuccessCallback, ErrorCallback"))
	void LinkAccount(const FString& UserId, const EXsollaTargetPlatform Platform, const FString& Code, const FOnRequestSuccess& SuccessCallback, const FOnAuthError& ErrorCallback);

	/** Authenticates platform account user
	 *
	 * @param UserId Identifier of platform account user.
	 * @param Platform Platform name.
	 * @param SuccessCallback Callback function called after succesfull user authentication on specified platform.
	 * @param ErrorCallback Callback function called after request resulted with an error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login", meta = (AutoCreateRefTerm = "SuccessCallback, ErrorCallback"))
	void AuthenticatePlatformAccountUser(const FString& UserId, const EXsollaTargetPlatform Platform, const FOnAuthUpdate& SuccessCallback, const FOnAuthError& ErrorCallback);

protected:
	void Default_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnRequestSuccess SuccessCallback, FOnAuthError ErrorCallback);
	void UserLogin_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnAuthUpdate SuccessCallback, FOnAuthError ErrorCallback);
	void TokenVerify_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnAuthUpdate SuccessCallback, FOnAuthError ErrorCallback);
	void SocialAuthUrl_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnSocialUrlReceived SuccessCallback, FOnAuthError ErrorCallback);
	void CrossAuth_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnAuthUpdate SuccessCallback, FOnAuthError ErrorCallback);
	void UpdateUserAttributes_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnRequestSuccess SuccessCallback, FOnAuthError ErrorCallback);
	void AccountLinkingCode_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnCodeReceived SuccessCallback, FOnAuthError ErrorCallback);
	void AuthConsoleAccountUser_HttpRequestComplete(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnAuthUpdate SuccessCallback, FOnAuthError ErrorCallback);

	/** Return true if error has happened */
	bool HandleRequestError(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded, FOnAuthError ErrorCallback);

private:
	/** Create http request and add Xsolla API meta */
	TSharedRef<IHttpRequest> CreateHttpRequest(const FString& Url, const EXsollaLoginRequestVerb Verb = EXsollaLoginRequestVerb::GET, const FString& Content = FString(), const FString& AuthToken = FString());

	/** Set a Json string array field named FieldName and value of Array */
	void SetStringArrayField(TSharedPtr<FJsonObject> Object, const FString& FieldName, const TArray<FString>& Array) const;

	/** Parse JWT token and get its payload as json object */
	bool ParseTokenPayload(const FString& Token, TSharedPtr<FJsonObject>& PayloadJsonObject) const;

	/** Get name of target platform */
	FString GetTargetPlatformName(EXsollaTargetPlatform Platform);

	/** Cached Xsolla project id */
	FString ProjectID;

	/** Cached Xsolla Login project id */
	FString LoginID;

public:
	/** Get user login state data */
	UFUNCTION(BlueprintPure, Category = "Xsolla|Login")
	FXsollaLoginData GetLoginData();

	/** Drop cache and cleanup login data */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	void DropLoginData();

	/** Get user ID from specified JWT token
	 *
	 * @param Token User authorization token.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	FString GetUserId(const FString& Token);

	/** Get token provider
	 *
	 * @param Token User authorization token.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	FString GetTokenProvider(const FString& Token);

	/** Get value of specified JWT token parameter
	 *
	 * @param Token User authorization token.
	 * @param Parameter Name of parameter which value should be extracted from token.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	FString GetTokenParameter(const FString& Token, const FString& Parameter);

	/** Check if specified JWT token represents master account
	 *
	 * @param Token User authorization token.
	 */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	bool IsMasterAccount(const FString& Token);

	/** Load save game and extract data */
	void LoadSavedData();

	/** Save cached data or reset one if RememberMe is false */
	void SaveData();

	/** Get pending social authentication url to be opened in browser */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	FString GetPendingSocialAuthenticationUrl() const;

	/** Get user attributes */
	UFUNCTION(BlueprintCallable, Category = "Xsolla|Login")
	TArray<FXsollaUserAttribute> GetUserAttributes();

protected:
	/** Keeps state of user login */
	FXsollaLoginData LoginData;

	/** Social authentication url to be opened in browser */
	FString PendingSocialAuthenticationUrl;

	/** Cached list of user attributes */
	TArray<FXsollaUserAttribute> UserAttributes;

protected:
	static const FString RegistrationEndpoint;
	static const FString LoginEndpoint;
	static const FString LoginSocialEndpoint;
	static const FString ResetPasswordEndpoint;

	static const FString ProxyRegistrationEndpoint;
	static const FString ProxyLoginEndpoint;
	static const FString ProxyResetPasswordEndpoint;

	static const FString ValidateTokenEndpoint;

	static const FString UserAttributesEndpoint;

	static const FString CrossAuthEndpoint;

	static const FString AccountLinkingCodeEndpoint;

private:
	UPROPERTY()
	TSubclassOf<UUserWidget> DefaultBrowserWidgetClass;
};
