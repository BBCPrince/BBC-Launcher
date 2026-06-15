using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Identity.Client;
using BBCLauncher.Configuration;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace BBCLauncher.Services
{
    public sealed class MicrosoftAuthService
    {
        private const string XboxUserAuthenticateUrl = "https://user.auth.xboxlive.com/user/authenticate";
        private const string XstsAuthorizeUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
        private const string MinecraftLoginWithXboxUrl = "https://api.minecraftservices.com/authentication/login_with_xbox";
        private const string MinecraftEntitlementsUrl = "https://api.minecraftservices.com/entitlements/mcstore";
        private const string MinecraftProfileUrl = "https://api.minecraftservices.com/minecraft/profile";

        private static readonly string[] MicrosoftScopes =
        {
            "XboxLive.signin",
            "offline_access",
        };

        private static readonly HttpClient Http = new HttpClient();

        private readonly LauncherConfig _config;
        private IPublicClientApplication _client;

        public event Action<string> AuthenticationStatusChanged;

        public MicrosoftAuthService(LauncherConfig config)
        {
            _config = config;
        }

        private IPublicClientApplication GetClient()
        {
            if (_client != null)
            {
                return _client;
            }

            if (string.IsNullOrWhiteSpace(_config.MicrosoftClientId) ||
                _config.MicrosoftClientId.Contains("REPLACE"))
            {
                throw new InvalidOperationException(
                    "Configure MicrosoftClientId in launcher.config.json with your Azure app registration.");
            }

            _client = PublicClientApplicationBuilder
                .Create(_config.MicrosoftClientId)
                .WithAuthority("https://login.microsoftonline.com/consumers")
                .Build();
            return _client;
        }

        public async Task<AuthResult> SignInAsync()
        {
            if (_config.SkipAuthenticationForDebug)
            {
#if DEBUG
                var debugSession = MinecraftSession.CreateDebug(_config.MicrosoftClientId);
                return AuthResult.Succeeded(
                    debugSession,
                    EntitlementResult.Succeeded("Local test mode: ownership check skipped by configuration."),
                    "Local test mode: sign-in skipped by configuration.");
#else
                return AuthResult.Failed("Local test authentication is disabled in release builds.");
#endif
            }

            try
            {
                AuthenticationResult microsoftResult = null;
                var signInMode = "Interactive";
                var accounts = await GetClient().GetAccountsAsync();
                var account = accounts.FirstOrDefault();
                if (account != null)
                {
                    try
                    {
                        microsoftResult = await GetClient()
                            .AcquireTokenSilent(MicrosoftScopes, account)
                            .ExecuteAsync();
                        signInMode = "Silent";
                    }
                    catch (MsalUiRequiredException)
                    {
                        microsoftResult = null;
                    }
                }

                if (microsoftResult == null)
                {
                    signInMode = "Device code";
                    microsoftResult = await GetClient()
                        .AcquireTokenWithDeviceCode(
                            MicrosoftScopes,
                            result =>
                            {
                                ReportAuthenticationStatus("Microsoft sign-in code: " + NormalizeDeviceCodeMessage(result.Message));
                                return Task.CompletedTask;
                            })
                        .ExecuteAsync();
                }

                var session = await BuildMinecraftSessionAsync(microsoftResult.AccessToken);
                var entitlement = EntitlementResult.Succeeded(
                    "Minecraft Java ownership verified: " + string.Join(", ", session.Entitlements));
                return AuthResult.Succeeded(
                    session,
                    entitlement,
                    signInMode + " Microsoft sign-in succeeded. Minecraft profile verified as " + session.Username + ".");
            }
            catch (MsalClientException ex) when (ex.ErrorCode == MsalError.AuthenticationCanceledError)
            {
                return AuthResult.Cancelled("Sign-in was cancelled.");
            }
            catch (MinecraftAuthException ex)
            {
                return AuthResult.Failed(ex.Message);
            }
            catch (Exception ex)
            {
                return AuthResult.Failed("Authentication failed: " + ex.Message);
            }
        }

        private void ReportAuthenticationStatus(string message)
        {
            if (!string.IsNullOrWhiteSpace(message))
            {
                AuthenticationStatusChanged?.Invoke(message);
            }
        }

        public async Task<EntitlementResult> VerifyMinecraftJavaOwnershipAsync(string accessToken)
        {
            if (_config.SkipAuthenticationForDebug)
            {
#if DEBUG
                return EntitlementResult.Succeeded("Local test mode: ownership check skipped by configuration.");
#else
                return EntitlementResult.Failed("Local test authentication is disabled in release builds.");
#endif
            }

            if (string.IsNullOrWhiteSpace(accessToken))
            {
                return EntitlementResult.Failed("Missing Minecraft access token.");
            }

            try
            {
                var entitlementNames = await ReadEntitlementNamesAsync(accessToken);
                if (HasMinecraftJavaEntitlement(entitlementNames))
                {
                    return EntitlementResult.Succeeded(
                        "Minecraft Java ownership verified: " + string.Join(", ", entitlementNames));
                }

                return EntitlementResult.Failed(
                    "This Microsoft account signed in, but Minecraft Java ownership was not found.");
            }
            catch (MinecraftAuthException ex)
            {
                return EntitlementResult.Failed(ex.Message);
            }
            catch (Exception ex)
            {
                return EntitlementResult.Failed("Minecraft ownership check failed: " + ex.Message);
            }
        }

        public Task<EntitlementResult> VerifyMinecraftJavaOwnershipAsync(MinecraftSession session)
        {
            if (session != null && session.HasJavaOwnership)
            {
                return Task.FromResult(EntitlementResult.Succeeded(
                    "Minecraft Java ownership verified: " + string.Join(", ", session.Entitlements)));
            }

            return VerifyMinecraftJavaOwnershipAsync(session == null ? null : session.MinecraftAccessToken);
        }

        private async Task<MinecraftSession> BuildMinecraftSessionAsync(string microsoftAccessToken)
        {
            var xbox = await AuthenticateXboxLiveWithFallbackAsync(microsoftAccessToken);
            var xsts = await AuthorizeXstsAsync(xbox.Token);
            var userHash = !string.IsNullOrWhiteSpace(xsts.UserHash) ? xsts.UserHash : xbox.UserHash;
            if (string.IsNullOrWhiteSpace(userHash))
            {
                throw new MinecraftAuthException("Xbox authentication succeeded but did not return a user hash.");
            }

            var minecraftAccessToken = await LoginWithMinecraftServicesAsync(userHash, xsts.Token);
            var entitlements = await ReadEntitlementNamesAsync(minecraftAccessToken);
            if (!HasMinecraftJavaEntitlement(entitlements))
            {
                throw new MinecraftAuthException(
                    "This Microsoft account signed in, but Minecraft Java ownership was not found.");
            }

            var profile = await ReadMinecraftProfileAsync(minecraftAccessToken);
            var xuid = FirstNonEmpty(
                xsts.Xuid,
                xbox.Xuid,
                TryReadJwtClaim(minecraftAccessToken, "xuid"),
                TryReadJwtClaim(minecraftAccessToken, "xid"));

            return new MinecraftSession
            {
                MicrosoftAccessToken = microsoftAccessToken,
                XboxUserToken = xbox.Token,
                XboxUserHash = userHash,
                XstsToken = xsts.Token,
                MinecraftAccessToken = minecraftAccessToken,
                Username = profile.Username,
                Uuid = NormalizeUuid(profile.Uuid),
                Xuid = string.IsNullOrWhiteSpace(xuid) ? "0" : xuid,
                ClientId = _config.MicrosoftClientId,
                Entitlements = entitlements,
                HasJavaOwnership = true,
            };
        }

        private async Task<XboxTokenResult> AuthenticateXboxLiveWithFallbackAsync(string microsoftAccessToken)
        {
            try
            {
                return await AuthenticateXboxLiveAsync(microsoftAccessToken, prefixRpsTicket: true);
            }
            catch (MinecraftAuthException ex) when (ex.StatusCode == HttpStatusCode.BadRequest || ex.StatusCode == HttpStatusCode.Unauthorized)
            {
                return await AuthenticateXboxLiveAsync(microsoftAccessToken, prefixRpsTicket: false);
            }
        }

        private async Task<XboxTokenResult> AuthenticateXboxLiveAsync(string microsoftAccessToken, bool prefixRpsTicket)
        {
            if (string.IsNullOrWhiteSpace(microsoftAccessToken))
            {
                throw new MinecraftAuthException("Missing Microsoft access token.");
            }

            var payload = new JObject
            {
                ["Properties"] = new JObject
                {
                    ["AuthMethod"] = "RPS",
                    ["SiteName"] = "user.auth.xboxlive.com",
                    ["RpsTicket"] = prefixRpsTicket ? "d=" + microsoftAccessToken : microsoftAccessToken,
                },
                ["RelyingParty"] = "http://auth.xboxlive.com",
                ["TokenType"] = "JWT",
            };

            var json = await PostJsonAsync(XboxUserAuthenticateUrl, payload, "Xbox Live authentication");
            return ReadXboxToken(json, "Xbox Live authentication");
        }

        private async Task<XboxTokenResult> AuthorizeXstsAsync(string xboxUserToken)
        {
            var payload = new JObject
            {
                ["Properties"] = new JObject
                {
                    ["SandboxId"] = "RETAIL",
                    ["UserTokens"] = new JArray(xboxUserToken),
                },
                ["RelyingParty"] = "rp://api.minecraftservices.com/",
                ["TokenType"] = "JWT",
            };

            var json = await PostJsonAsync(XstsAuthorizeUrl, payload, "Xbox XSTS authorization");
            return ReadXboxToken(json, "Xbox XSTS authorization");
        }

        private async Task<string> LoginWithMinecraftServicesAsync(string userHash, string xstsToken)
        {
            var payload = new JObject
            {
                ["identityToken"] = "XBL3.0 x=" + userHash + ";" + xstsToken,
                ["ensureLegacyEnabled"] = true,
            };

            var json = await PostJsonAsync(MinecraftLoginWithXboxUrl, payload, "Minecraft Services login");
            var token = json.Value<string>("access_token");
            if (string.IsNullOrWhiteSpace(token))
            {
                throw new MinecraftAuthException("Minecraft Services login did not return an access token.");
            }

            return token;
        }

        private async Task<List<string>> ReadEntitlementNamesAsync(string minecraftAccessToken)
        {
            var json = await GetJsonAsync(
                MinecraftEntitlementsUrl,
                "Minecraft entitlement check",
                minecraftAccessToken);
            var items = json["items"] as JArray;
            if (items == null)
            {
                return new List<string>();
            }

            return items
                .Select(item => item == null ? null : item.Value<string>("name"))
                .Where(name => !string.IsNullOrWhiteSpace(name))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        private async Task<MinecraftProfileResult> ReadMinecraftProfileAsync(string minecraftAccessToken)
        {
            var json = await GetJsonAsync(
                MinecraftProfileUrl,
                "Minecraft profile lookup",
                minecraftAccessToken);
            var id = json.Value<string>("id");
            var name = json.Value<string>("name");
            if (string.IsNullOrWhiteSpace(id) || string.IsNullOrWhiteSpace(name))
            {
                throw new MinecraftAuthException("Minecraft profile lookup did not return a profile name and UUID.");
            }

            return new MinecraftProfileResult
            {
                Uuid = id,
                Username = name,
            };
        }

        private static bool HasMinecraftJavaEntitlement(IEnumerable<string> entitlementNames)
        {
            return entitlementNames != null && entitlementNames.Any(name =>
                string.Equals(name, "game_minecraft", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(name, "product_minecraft", StringComparison.OrdinalIgnoreCase));
        }

        private static XboxTokenResult ReadXboxToken(JObject json, string stageName)
        {
            var token = json.Value<string>("Token");
            var xui = json["DisplayClaims"]?["xui"] as JArray;
            var firstClaim = xui == null ? null : xui.FirstOrDefault() as JObject;
            var userHash = firstClaim == null ? null : firstClaim.Value<string>("uhs");
            var xuid = firstClaim == null ? null : firstClaim.Value<string>("xid");
            if (string.IsNullOrWhiteSpace(token))
            {
                throw new MinecraftAuthException(stageName + " did not return a token.");
            }

            return new XboxTokenResult
            {
                Token = token,
                UserHash = userHash,
                Xuid = xuid,
            };
        }

        private static async Task<JObject> PostJsonAsync(string url, JObject payload, string stageName)
        {
            using (var request = new HttpRequestMessage(HttpMethod.Post, url))
            {
                request.Headers.Accept.ParseAdd("application/json");
                request.Content = new StringContent(
                    payload.ToString(Formatting.None),
                    Encoding.UTF8,
                    "application/json");
                using (var response = await Http.SendAsync(request).ConfigureAwait(false))
                {
                    return await ReadJsonResponseAsync(response, stageName).ConfigureAwait(false);
                }
            }
        }

        private static async Task<JObject> GetJsonAsync(string url, string stageName, string bearerToken)
        {
            using (var request = new HttpRequestMessage(HttpMethod.Get, url))
            {
                request.Headers.Accept.ParseAdd("application/json");
                request.Headers.Authorization =
                    new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", bearerToken);
                using (var response = await Http.SendAsync(request).ConfigureAwait(false))
                {
                    return await ReadJsonResponseAsync(response, stageName).ConfigureAwait(false);
                }
            }
        }

        private static async Task<JObject> ReadJsonResponseAsync(HttpResponseMessage response, string stageName)
        {
            var body = response.Content == null
                ? string.Empty
                : await response.Content.ReadAsStringAsync().ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                throw CreateServiceException(stageName, response.StatusCode, body);
            }

            if (string.IsNullOrWhiteSpace(body))
            {
                return new JObject();
            }

            try
            {
                var token = JToken.Parse(body);
                var obj = token as JObject;
                if (obj != null)
                {
                    return obj;
                }
            }
            catch (JsonException ex)
            {
                throw new MinecraftAuthException(stageName + " returned invalid JSON: " + ex.Message);
            }

            throw new MinecraftAuthException(stageName + " returned JSON that was not an object.");
        }

        private static MinecraftAuthException CreateServiceException(string stageName, HttpStatusCode statusCode, string body)
        {
            JObject json = null;
            if (!string.IsNullOrWhiteSpace(body))
            {
                try
                {
                    json = JObject.Parse(body);
                }
                catch (JsonException)
                {
                }
            }

            var xerr = ReadLong(json, "XErr");
            if (xerr.HasValue)
            {
                return new MinecraftAuthException(
                    stageName + " failed: " + DescribeXstsError(xerr.Value, json == null ? null : json.Value<string>("Redirect")),
                    statusCode,
                    xerr.Value);
            }

            var serviceMessage = FirstNonEmpty(
                json == null ? null : json.Value<string>("errorMessage"),
                json == null ? null : json.Value<string>("developerMessage"),
                json == null ? null : json.Value<string>("Message"),
                json == null ? null : json.Value<string>("error"));
            if (string.IsNullOrWhiteSpace(serviceMessage))
            {
                serviceMessage = string.IsNullOrWhiteSpace(body)
                    ? "no response body"
                    : TrimForLog(body, 300);
            }

            return new MinecraftAuthException(
                stageName + " failed (" + (int)statusCode + " " + statusCode + "): " + serviceMessage,
                statusCode);
        }

        private static string DescribeXstsError(long xerr, string redirect)
        {
            string message;
            switch (xerr)
            {
                case 2148916233:
                    message = "this Microsoft account does not have an Xbox profile yet. Sign in once at xbox.com, then try again.";
                    break;
                case 2148916235:
                    message = "Xbox Live is not available in this account's region.";
                    break;
                case 2148916236:
                case 2148916237:
                    message = "this Xbox account needs adult verification before it can use Xbox Live services.";
                    break;
                case 2148916238:
                    message = "this Xbox child account needs to be added to a family by an adult account.";
                    break;
                default:
                    message = "Xbox returned XErr " + xerr + ".";
                    break;
            }

            if (!string.IsNullOrWhiteSpace(redirect))
            {
                message += " Redirect: " + redirect;
            }

            return message;
        }

        private static long? ReadLong(JObject json, string propertyName)
        {
            if (json == null || json[propertyName] == null)
            {
                return null;
            }

            long value;
            return long.TryParse(json[propertyName].ToString(), out value)
                ? value
                : (long?)null;
        }

        private static string TryReadJwtClaim(string jwt, string claimName)
        {
            if (string.IsNullOrWhiteSpace(jwt))
            {
                return null;
            }

            var parts = jwt.Split('.');
            if (parts.Length < 2)
            {
                return null;
            }

            try
            {
                var payload = parts[1].Replace('-', '+').Replace('_', '/');
                switch (payload.Length % 4)
                {
                    case 2:
                        payload += "==";
                        break;
                    case 3:
                        payload += "=";
                        break;
                }

                var bytes = Convert.FromBase64String(payload);
                var json = JObject.Parse(Encoding.UTF8.GetString(bytes, 0, bytes.Length));
                return json.Value<string>(claimName);
            }
            catch
            {
                return null;
            }
        }

        private static string NormalizeUuid(string uuid)
        {
            return string.IsNullOrWhiteSpace(uuid)
                ? "00000000000000000000000000000000"
                : uuid.Replace("-", string.Empty);
        }

        private static string FirstNonEmpty(params string[] values)
        {
            return values == null
                ? null
                : values.FirstOrDefault(value => !string.IsNullOrWhiteSpace(value));
        }

        private static string TrimForLog(string value, int maxLength)
        {
            if (string.IsNullOrEmpty(value) || value.Length <= maxLength)
            {
                return value;
            }

            return value.Substring(0, maxLength) + "...";
        }

        private static string NormalizeDeviceCodeMessage(string message)
        {
            if (string.IsNullOrWhiteSpace(message))
            {
                return "Follow the Microsoft device-code sign-in prompt.";
            }

            return message
                .Replace("\r", " ")
                .Replace("\n", " ")
                .Trim();
        }
    }

    public sealed class MinecraftSession
    {
        public string MicrosoftAccessToken { get; set; }
        public string XboxUserToken { get; set; }
        public string XboxUserHash { get; set; }
        public string XstsToken { get; set; }
        public string MinecraftAccessToken { get; set; }
        public string Username { get; set; }
        public string Uuid { get; set; }
        public string Xuid { get; set; }
        public string ClientId { get; set; }
        public List<string> Entitlements { get; set; } = new List<string>();
        public bool HasJavaOwnership { get; set; }

        public static MinecraftSession CreateDebug(string clientId)
        {
            return new MinecraftSession
            {
                MinecraftAccessToken = "local-test-auth-token",
                Username = "LocalTestPlayer",
                Uuid = "00000000000000000000000000000000",
                Xuid = "0",
                ClientId = string.IsNullOrWhiteSpace(clientId) ? "local-test-client" : clientId,
                Entitlements = new List<string> { "local_test_auth" },
                HasJavaOwnership = true,
            };
        }
    }

    public sealed class AuthResult
    {
        public bool IsSuccess { get; private set; }
        public bool IsCancelled { get; private set; }
        public string AccessToken { get; private set; }
        public string Username { get; private set; }
        public string Uuid { get; private set; }
        public string Xuid { get; private set; }
        public string ClientId { get; private set; }
        public MinecraftSession Session { get; private set; }
        public EntitlementResult Entitlement { get; private set; }
        public string Message { get; private set; }

        public static AuthResult Succeeded(MinecraftSession session, EntitlementResult entitlement, string message)
        {
            return new AuthResult
            {
                IsSuccess = true,
                AccessToken = session == null ? null : session.MinecraftAccessToken,
                Username = session == null ? null : session.Username,
                Uuid = session == null ? null : session.Uuid,
                Xuid = session == null ? null : session.Xuid,
                ClientId = session == null ? null : session.ClientId,
                Session = session,
                Entitlement = entitlement,
                Message = message,
            };
        }

        public static AuthResult Cancelled(string message)
        {
            return new AuthResult { IsCancelled = true, Message = message };
        }

        public static AuthResult Failed(string message)
        {
            return new AuthResult { Message = message };
        }
    }

    public sealed class EntitlementResult
    {
        public bool IsOwned { get; private set; }
        public string Message { get; private set; }

        public static EntitlementResult Succeeded(string message)
        {
            return new EntitlementResult { IsOwned = true, Message = message };
        }

        public static EntitlementResult Failed(string message)
        {
            return new EntitlementResult { Message = message };
        }
    }

    internal sealed class MinecraftAuthException : Exception
    {
        public MinecraftAuthException(string message, HttpStatusCode? statusCode = null, long? xerr = null)
            : base(message)
        {
            StatusCode = statusCode;
            XErr = xerr;
        }

        public HttpStatusCode? StatusCode { get; private set; }
        public long? XErr { get; private set; }
    }

    internal sealed class XboxTokenResult
    {
        public string Token { get; set; }
        public string UserHash { get; set; }
        public string Xuid { get; set; }
    }

    internal sealed class MinecraftProfileResult
    {
        public string Uuid { get; set; }
        public string Username { get; set; }
    }
}
